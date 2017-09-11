// Do a single pass of Vanilla, calculating CBRs for every single hand.
// CBRs are counterfactual best response values.
//
// CBRs are written out in normal hand order on every street except max
// street.  On the final street, CBRs are written out for hands as sorted by
// hand strength.
//
// CBRs for a hand depend only on the opponent's strategy above and below
// a given node.  P1's CBRs are distinct from P2's CBRs.
//
// Right now run_cfrp produces trunk sumprobs files for the preflop and
// flop, and compressed sumprobs files for the turn and river.  I can't
// easily split on the flop now if I want each file of sumprobs to belong
// to a single thread.  So split on the turn instead?
//
// Eventually I might want to switch to the same form of multithreading
// as run_cfrp and run_cfrp_rgbr.  In other words, have a bunch of
// subgame objects that get spawned.
//
// Splitting is not supported right now.  Code currently assumes that
// kSplitStreet is the same as the split street used in running CFR.  If
// I really want that, use SplitStreet parameter.  But then the question
// might be why not go all the way and use subgames just like CFR.
//
// On the other hand, this form of splitting may be simpler.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cbr_thread.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "vcfr.h"

using namespace std;

CBRThread::CBRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
		     const CFRConfig &cc, const Buckets &buckets,
		     const BettingTree *betting_tree, bool cfrs,
		     unsigned int p, HandTree *trunk_hand_tree,
		     unsigned int thread_index, unsigned int num_threads,
		     unsigned int it, CBRThread **threads, bool trunk) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  cfrs_ = cfrs;
  p_ = p;
  trunk_hand_tree_ = trunk_hand_tree;
  thread_index_ = thread_index;
  it_ = it;
  threads_ = threads;
  regrets_.reset(nullptr);


  final_hand_vals_ = nullptr;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = ! cfrs_;
  }
  br_current_ = false;
  value_calculation_ = true;
  // Can't skip succ even if succ_sum_opp_probs is zero.  I need to write
  // out CBR values at every node.
  prune_ = false;

  // Always want sumprobs for the opponent(s).  Want sumprobs for target player
  // as well when computing CFRs (as opposed to CBRs).
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    players[p] = cfrs_ || p != p_;
  }
  if (trunk) {
    root_bd_st_ = 0;
    root_bd_ = 0;
    hand_tree_ = trunk_hand_tree_;
    // Should handle asymmetric systems
    // Should honor sumprobs_streets_
    sumprobs_.reset(new CFRValues(players.get(), true, nullptr, betting_tree_,
				  0, 0, card_abstraction_, buckets_,
				  compressed_streets_));

    char dir[500];
    sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(),
	    card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	    Game::NumSuits(), Game::MaxStreet(),
	    betting_abstraction_.BettingAbstractionName().c_str(),
	    cfr_config_.CFRConfigName().c_str());
    if (betting_abstraction_.Asymmetric()) {
      if (cfrs_) {
	fprintf(stderr, "Don't support CFRs and asymmetric\n");
	exit(-1);
      }
      char buf[100];
      sprintf(buf, ".p%u", p_^1);
      strcat(dir, buf);
    }
    sumprobs_->Read(dir, it_, betting_tree_->Root(),
		    betting_tree_->Root()->NonterminalID(), kMaxUInt);

    unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
    bucketed_ = false;
    for (unsigned int st = 0; st <= max_street; ++st) {
      bucketed_streets[st] = ! buckets_.None(st);
      if (bucketed_streets[st]) bucketed_ = true;
    }
    if (bucketed_) {
      // Current strategy always uses doubles
      // Shouldn't have to create this.
      current_strategy_.reset(new CFRValues(players.get(), false,
					    bucketed_streets.get(),
					    betting_tree_, 0, 0,
					    card_abstraction_, buckets_,
					    compressed_streets_));
      current_strategy_->AllocateAndClearDoubles(betting_tree_->Root(),
						 kMaxUInt);
      SetCurrentStrategy(betting_tree_->Root());
    } else {
      current_strategy_.reset(nullptr);
    }
  } else {
    // We are not the trunk thread
    root_bd_st_ = kSplitStreet;
    root_bd_ = kMaxUInt;
    hand_tree_ = nullptr;
    sumprobs_.reset(nullptr);
  }
}

CBRThread::~CBRThread(void) {
  // Don't delete hand_tree_.  In the trunk it is identical to trunk_hand_tree_
  // which is owned by the caller (CBRBuilder).  In the endgames it is
  // deleted in AfterSplit().
  delete [] final_hand_vals_;
}

void CBRThread::WriteValues(Node *node, unsigned int gbd, double *vals) {
  char dir[500], dir2[500], buf[500];
  unsigned int street = node->Street();
  unsigned int nonterminal_id = node->NonterminalID();
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s",
	  Files::NewCFRBase(), Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    if (cfrs_) {
      fprintf(stderr, "Don't support CFRs and asymmetric\n");
      exit(-1);
    }
    char buf2[100];
    sprintf(buf2, ".p%u", p_);
    strcat(dir, buf2);
  }
  sprintf(dir2, "%s/%s.%u.p%u/%u.%u.%u", dir, cfrs_ ? "cfrs" : "cbrs",
	  it_, p_, nonterminal_id, street, node->PlayerActing());
  Mkdir(dir2);
  sprintf(buf, "%s/vals.%u", dir2, gbd);
  Writer writer(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    writer.WriteFloat((float)vals[i]);
  }
}

double *CBRThread::OurChoice(Node *node, unsigned int lbd, double *opp_probs,
			     double sum_opp_probs, double *total_card_probs) {
  double *vals = VCFR::OurChoice(node, lbd, opp_probs, sum_opp_probs,
				 total_card_probs);

  unsigned int st = node->Street();
  unsigned int gbd = 0;
  if (st > 0) {
    gbd = BoardTree::GlobalIndex(root_bd_st_, root_bd_, st, lbd);
  }
#if 0
  if (node->NonterminalID() == 0 && st == 1 && node->PlayerActing() == 0 &&
      gbd == 0) {
    double sop = 0;
    unsigned int max_card1 = Game::MaxCard() + 1;
    const CanonicalCards *hands = hand_tree_->Hands(st, lbd);
    unsigned int num_hands = hands->NumRaw();
    for (unsigned int i = 0; i < num_hands; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      sop += opp_probs[enc];
    }
  }
#endif
  WriteValues(node, gbd, vals);
  
  return vals;
}

double *CBRThread::OppChoice(Node *node, unsigned int lbd, double *opp_probs,
			     double sum_opp_probs, double *total_card_probs) {
  double *vals = VCFR::OppChoice(node, lbd, opp_probs, sum_opp_probs,
				 total_card_probs);

  unsigned int st = node->Street();
  unsigned int gbd = 0;
  if (st > 0) {
    gbd = BoardTree::GlobalIndex(root_bd_st_, root_bd_, st, lbd);
  }
  WriteValues(node, gbd, vals);
  
  return vals;
}

// Fork off threads on street kSplitStreet
double *CBRThread::Split(Node *node, unsigned int bd, double *opp_probs) {
  unsigned int nst = node->Street();
  unsigned int pst = nst - 1;
  unsigned int pred_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  double *vals = new double[pred_num_hole_card_pairs];
  for (unsigned int i = 0; i < pred_num_hole_card_pairs; ++i) vals[i] = 0;

  for (unsigned int t = 0; t < num_threads_; ++t) {
    threads_[t]->SetSplitNode(node);
    threads_[t]->SetSplitBd(bd);
    threads_[t]->SetOppReachProbs(opp_probs);
    threads_[t]->Run();
  }
  for (unsigned int t = 0; t < num_threads_; ++t) {
    threads_[t]->Join();
  }
  for (unsigned int t = 0; t < num_threads_; ++t) {
    double *t_hand_vals = threads_[t]->FinalHandVals();
    for (unsigned int i = 0; i < pred_num_hole_card_pairs; ++i) {
      vals[i] += t_hand_vals[i];
    }
  }

  return vals;
}

double *CBRThread::Process(Node *node, unsigned int lbd, double *opp_probs,
			   double sum_opp_probs, double *total_card_probs,
			   unsigned int last_st) {
  unsigned int st = node->Street();
  if (st > last_st && st == kSplitStreet) {
    return Split(node, lbd, opp_probs);
  } else {
#if 0
    if (node->NonterminalID() == 0 && st == 1 && node->PlayerActing() == 0 &&
	lbd == 0 && last_st == 1) {
      double sop = 0;
      unsigned int max_card1 = Game::MaxCard() + 1;
      const CanonicalCards *hands = hand_tree_->Hands(st, lbd);
      unsigned int num_hands = hands->NumRaw();
      for (unsigned int i = 0; i < num_hands; ++i) {
	const Card *cards = hands->Cards(i);
	Card hi = cards[0];
	Card lo = cards[1];
	unsigned int enc = hi * max_card1 + lo;
	sop += opp_probs[enc];
      }
      fprintf(stderr, "P%u sop %f\n", p_, sop);
    }
#endif
    return VCFR::Process(node, lbd, opp_probs, sum_opp_probs,
			 total_card_probs, last_st);
  }
}

static void *thread_run(void *v_t) {
  CBRThread *t = (CBRThread *)v_t;
  t->AfterSplit();
  return NULL;
}

void CBRThread::Run(void) {
  pthread_create(&pthread_id_, NULL, thread_run, this);
}

void CBRThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

// We replicate a lot of the logic from CFR::StreetInitial().  But we can't
// use CFR::StreetInitial() because we are only interested in a subset of
// the boards.
void CBRThread::AfterSplit(void) {
  fprintf(stderr, "AfterSplit %p\n", split_node_);
  unsigned int nst = split_node_->Street();
  unsigned int pst = nst - 1;
  root_bd_st_ = nst;

  unsigned int pred_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  fprintf(stderr, "AfterSplit 1.1\n");
  delete [] final_hand_vals_;
  fprintf(stderr, "AfterSplit 1.2\n");
  final_hand_vals_ = new double[pred_num_hole_card_pairs];
  for (unsigned int i = 0; i < pred_num_hole_card_pairs; ++i) {
    final_hand_vals_[i] = 0;
  }
  fprintf(stderr, "AfterSplit 1.3\n");

  unsigned int max_card = Game::MaxCard();
  unsigned int num_encodings = (max_card + 1) * (max_card + 1);
  unsigned int *prev_canons = new unsigned int[num_encodings];
  fprintf(stderr, "AfterSplit 1.4\n");
  const CanonicalCards *pred_hands = trunk_hand_tree_->Hands(pst, split_bd_);
  fprintf(stderr, "AfterSplit2\n");

  // Assumes we are splitting at the flop
  for (unsigned int ph = 0; ph < pred_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) > 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      Card hi = prev_cards[0];
      Card lo = prev_cards[1];
      unsigned int prev_encoding = hi * (max_card + 1) + lo;
      prev_canons[prev_encoding] = ph;
    }
  }
  for (unsigned int ph = 0; ph < pred_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      Card hi = prev_cards[0];
      Card lo = prev_cards[1];
      unsigned int prev_encoding = hi * (max_card + 1) + lo;
      unsigned int pc = prev_canons[pred_hands->Canon(ph)];
      prev_canons[prev_encoding] = pc;
    }
  }
  fprintf(stderr, "AfterSplit3\n");

  unsigned int max_street = Game::MaxStreet();
  bool *subtree_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    subtree_streets[st] = st >= nst;
  }

  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
#if 0
  if (betting_abstraction_.Asymmetric()) {
    if (target_p_) strcat(dir, ".p1");
    else           strcat(dir, ".p2");
  }
#endif

  fprintf(stderr, "AfterSplit4\n");
  BettingTree *subtree = BettingTree::BuildSubtree(split_node_);
  fprintf(stderr, "AfterSplit5\n");
  
  unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, split_bd_, nst);
  unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, split_bd_, nst);
  for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
    fprintf(stderr, "AfterSplit6 %u\n", ngbd);
    unsigned int nlbd;
    if (root_bd_st_ == 0) {
      nlbd = ngbd;
    } else {
      nlbd = BoardTree::LocalIndex(root_bd_st_, root_bd_, nst, ngbd);
    }
    fprintf(stderr, "AfterSplit7 %u\n", nlbd);
    if (ngbd % num_threads_ != thread_index_) continue;
    root_bd_ = ngbd;
    hand_tree_ = new HandTree(nst, ngbd, max_street);
    // Should handle asymmetric systems
    // Should honor sumprobs_streets_
    fprintf(stderr, "AfterSplit8\n");
    // Always want sumprobs for the opponent.  Want sumprobs for target player
    // as well when computing CFRs (as opposed to CBRs).
    unsigned int num_players = Game::NumPlayers();
    unique_ptr<bool []> players(new bool[num_players]);
    for (unsigned int p = 0; p < num_players; ++p) {
      players[p] = cfrs_ || p != p_;
    }
    sumprobs_.reset(new CFRValues(players.get(), true, subtree_streets,
				  subtree, ngbd, root_bd_st_,
				  card_abstraction_, buckets_,
				  compressed_streets_));
    // We are assuming sumprobs are split in the same way as we want to
    // split CBR calculation.
    fprintf(stderr, "AfterSplit9\n");
    sumprobs_->Read(dir, it_, subtree->Root(), split_node_->NonterminalID(),
		    kMaxUInt);
    fprintf(stderr, "AfterSplit10\n");

    unsigned int num_variants = BoardTree::NumVariants(nst, ngbd);
    fprintf(stderr, "AfterSplit11\n");
    // We pass in the *local* board index which is zero, not ngbd which is
    // the global board index.
    // I can pass unset values for sum_opp_probs and total_card_probs.  I
    // know I will come across an opp choice node before getting to a
    // terminal node.
    // Or maybe it's OK because we are not updating sum-opp-probs in a CBR
    // calculation.
    double *next_vals = Process(subtree->Root(), nlbd, opp_reach_probs_, 0,
				NULL, nst);
    sumprobs_.reset(nullptr);

    const CanonicalCards *next_hands = hand_tree_->Hands(nst, 0);
    unsigned int num_next_hands = next_hands->NumRaw();
    for (unsigned int nh = 0; nh < num_next_hands; ++nh) {
      const Card *cards = next_hands->Cards(nh);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * (max_card + 1) + lo;
      unsigned int prev_canon = prev_canons[enc];
      final_hand_vals_[prev_canon] += num_variants * next_vals[nh];
    }
    delete [] next_vals;
    delete hand_tree_;
    hand_tree_ = NULL;
  }

  delete subtree;
  delete [] subtree_streets;

  // Scale down the values of the previous-street canonical hands
  double scale_down = Game::StreetPermutations(nst);
  for (unsigned int ph = 0; ph < pred_num_hole_card_pairs; ++ph) {
    unsigned int prev_hand_variants = pred_hands->NumVariants(ph);
    if (prev_hand_variants > 0) {
      // Is this doing the right thing?
      final_hand_vals_[ph] /= scale_down * prev_hand_variants;
    }
  }
  // Copy the canonical hand values to the non-canonical
  for (unsigned int ph = 0; ph < pred_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      final_hand_vals_[ph] =
	final_hand_vals_[prev_canons[pred_hands->Canon(ph)]];
    }
  }

  delete [] prev_canons;
}

double CBRThread::Go(void) {
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card = Game::MaxCard();
  unsigned int num;
  if (num_hole_cards == 1) num = max_card + 1;
  else                     num = (max_card + 1) * (max_card + 1);
  double *opp_probs = new double[num];
  for (unsigned int i = 0; i < num; ++i) opp_probs[i] = 1.0;
  time_t start_t = time(NULL);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  double sum_opp_probs;
  double *total_card_probs = new double[num_hole_card_pairs];
  const CanonicalCards *hands = hand_tree_->Hands(0, 0);
  CommonBetResponseCalcs(0, hands, opp_probs, &sum_opp_probs,
			 total_card_probs);
  double *vals = Process(betting_tree_->Root(), 0, opp_probs, sum_opp_probs,
			 total_card_probs, 0);
  delete [] total_card_probs;
  // EVs for our hands are summed over all opponent hole card pairs.  To
  // compute properly normalized EV, need to divide by that number.
  unsigned int num_cards_in_deck = Game::NumCardsInDeck();
  unsigned int num_remaining = num_cards_in_deck - num_hole_cards;
  unsigned int num_opp_hole_card_pairs;
  if (num_hole_cards == 1) {
    num_opp_hole_card_pairs = num_remaining;
  } else {
    num_opp_hole_card_pairs = num_remaining * (num_remaining - 1) / 2;
  }
  double sum = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    sum += vals[i] / num_opp_hole_card_pairs;
  }
  double ev = sum / num_hole_card_pairs;
  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  printf("Process took %.1f seconds\n", diff_sec);
  fflush(stdout);
  delete [] opp_probs;
  delete [] vals;
  return ev;
}
