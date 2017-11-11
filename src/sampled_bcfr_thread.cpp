// BCFR values are "bucketed counterfactual values".  The code here generates
// *sampled* bucket-level CFR values whereas bcfr_thread.cpp generates exact
// values.
//
// The BCFR values are written out in normal hand order on every street except
// the final street.  On the final street, the values are written out for hands
// as sorted by hand strength.
//
// The values for a hand depend only on the opponent's strategy above and below
// a given node, and our strategy below the node.  P0's values are distinct
// from P1's values.

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "canonical.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "rand.h"
#include "sampled_bcfr_thread.h"
#include "vcfr.h"

using namespace std;

SampledBCFRThread::SampledBCFRThread(const CardAbstraction &ca,
				     const BettingAbstraction &ba,
				     const CFRConfig &cc,
				     const Buckets &buckets,
				     const BettingTree *betting_tree,
				     unsigned int p,
				     HandTree *trunk_hand_tree,
				     unsigned int thread_index,
				     unsigned int num_threads, unsigned int it,
				     unsigned int sample_st,
				     unsigned int num_to_sample,
				     double *****bucket_sum_vals,
				     unsigned int ****bucket_counts,
				     SampledBCFRThread **threads, bool trunk) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  p_ = p;
  trunk_hand_tree_ = trunk_hand_tree;
  thread_index_ = thread_index;
  it_ = it;
  sample_st_ = sample_st;
  num_to_sample_ = num_to_sample;
  bucket_sum_vals_ = bucket_sum_vals;
  bucket_counts_ = bucket_counts;
  threads_ = threads;

  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = false;
  }
  board_counts_.reset(new unsigned int [max_street + 1]);
  board_counts_[0] = 1;
  br_current_ = false;
  value_calculation_ = true;
  prune_ = false;

  regrets_.reset(nullptr);
  if (trunk) {
    root_bd_st_ = 0;
    root_bd_ = 0;
    hand_tree_ = trunk_hand_tree_;
    // Should handle asymmetric systems
    // Should honor sumprobs_streets_
    sumprobs_.reset(new CFRValues(nullptr, true, nullptr, betting_tree_, 0, 0,
				  card_abstraction_, buckets_,
				  compressed_streets_));

    char dir[500];
    sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(), Game::NumPlayers(),
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
    sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);

    unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
    bucketed_ = false;
    for (unsigned int st = 0; st <= max_street; ++st) {
      bucketed_streets[st] = ! buckets_.None(st);
      if (bucketed_streets[st]) bucketed_ = true;
    }
    if (bucketed_) {
      // Current strategy always uses doubles
      // This doesn't generalize to multiplayer
      current_strategy_.reset(new CFRValues(nullptr, false,
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
    fprintf(stderr, "Multithreading not supported yet\n");
    exit(-1);
#if 0
    // We are not the trunk thread
    root_bd_st_ = kSplitStreet;
    root_bd_ = kMaxUInt;
    hand_tree_ = nullptr;
    sumprobs_.reset(nullptr);
#endif
  }

  split_vals_ = nullptr;
}

SampledBCFRThread::~SampledBCFRThread(void) {
  delete [] split_vals_;

  // Don't delete hand_tree_.  In the trunk it is identical to trunk_hand_tree_
  // which is owned by the caller.
}


double *SampledBCFRThread::OurChoice(Node *node, unsigned int lbd,
				     double *opp_probs, double sum_opp_probs,
				     double *total_card_probs,
				     unsigned int **street_buckets,
				     const string &action_sequence) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int board_count;
  if (st < sample_st_) {
    board_count = board_counts_[st];
  } else {
    board_count = board_counts_[sample_st_ - 1];
  }
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    unsigned int b = street_buckets[st][i];
    bucket_counts_[st][pa][nt][b] += board_count;
    if (bucket_sum_vals_[st][pa][nt][b] == nullptr) {
      bucket_sum_vals_[st][pa][nt][b] = new double[num_succs];
      for (unsigned int s = 0; s < num_succs; ++s) {
	bucket_sum_vals_[st][pa][nt][b][s] = 0;
      }
    }
  }
  
  double **succ_vals = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    succ_vals[s] = Process(node->IthSucc(s), lbd, opp_probs, sum_opp_probs,
			   total_card_probs, street_buckets,
			   action_sequence + action, st);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      // Need to scale by board count, right?
      bucket_sum_vals_[st][pa][nt][b][s] += succ_vals[s][i] * board_count;
    }
  }

  double *vals;
  if (num_succs == 1) {
    vals = succ_vals[0];
    succ_vals[0] = nullptr;
  } else {
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
    double *d_all_current_probs;
    current_strategy_->Values(p_, st, nt, &d_all_current_probs);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      double *my_current_probs = d_all_current_probs + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	vals[i] += succ_vals[s][i] * my_current_probs[s];
      }
    }
  }
  
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_vals[s];
  }
  delete [] succ_vals;

  return vals;
}

void SampledBCFRThread::AfterSplit(void) {
  unsigned int nst = split_node_->Street();
  unsigned int pst = nst - 1;
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_next_hole_card_pairs = Game::NumHoleCardPairs(nst);
  unsigned int num_prev_hole_card_pairs = Game::NumHoleCardPairs(pst);
  if (split_vals_ == nullptr) {
    split_vals_ = new double[num_prev_hole_card_pairs];
  }
  for (unsigned int i = 0; i < num_prev_hole_card_pairs; ++i) {
    split_vals_[i] = 0;
  }
  unsigned int num_boards = BoardTree::NumBoards(nst);
  unsigned int **street_buckets = InitializeStreetBuckets();
  for (unsigned int bd = thread_index_; bd < num_boards; bd += num_threads_) {
    board_counts_[nst] = BoardTree::BoardCount(nst, bd);
    SetStreetBuckets(nst, bd, street_buckets);
    double *vals = Process(split_node_, 0, split_opp_probs_, 0, nullptr, 
			   street_buckets, split_action_sequence_, nst);
    unsigned int board_variants = BoardTree::NumVariants(nst, bd);
    const CanonicalCards *hands = hand_tree_->Hands(nst, bd);
    for (unsigned int i = 0; i < num_next_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      unsigned int prev_canon = split_prev_canons_[enc];
      split_vals_[prev_canon] += vals[i] * board_variants;
    }
    delete [] vals;
  }
  DeleteStreetBuckets(street_buckets);
}

static void *sbcfr_thread_run(void *v_t) {
  SampledBCFRThread *t = (SampledBCFRThread *)v_t;
  t->AfterSplit();
  return NULL;
}

void SampledBCFRThread::Run(void) {
  pthread_create(&pthread_id_, NULL, sbcfr_thread_run, this);
}

void SampledBCFRThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

double *SampledBCFRThread::StreetInitial(Node *node, unsigned int plbd,
					 double *opp_probs,
					 unsigned int **street_buckets,
					 const string &action_sequence) {
  unsigned int nst = node->Street();
  unsigned int pst = nst - 1;
  unsigned int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  const CanonicalCards *pred_hands = hand_tree_->Hands(pst, plbd);
  Card max_card = Game::MaxCard();
  unsigned int num_enc = (max_card + 1) * (max_card + 1);
  unsigned int *prev_canons = new unsigned int[num_enc];
  double *vals = new double[prev_num_hole_card_pairs];
  for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) > 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_enc = prev_cards[0] * (max_card + 1) + prev_cards[1];
      prev_canons[prev_enc] = ph;
    }
  }
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_enc = prev_cards[0] * (max_card + 1) + prev_cards[1];
      unsigned int pc = prev_canons[pred_hands->Canon(ph)];
      prev_canons[prev_enc] = pc;
    }
  }

  if (nst == sample_st_) {
    if (Game::NumCardsForStreet(nst) != 1) {
      fprintf(stderr, "Expect to sample only on street with one card\n");
      exit(-1);
    }
    // Sample just one board
    // First, select a river card at random
    unsigned int pgbd;
    if (root_bd_st_ == 0) {
      pgbd = plbd;
    } else {
      pgbd = BoardTree::GlobalIndex(root_bd_st_, root_bd_, pst, plbd);
    }
    const Card *prev_board = BoardTree::Board(pst, pgbd);
    unsigned int num_prev_board_cards = Game::NumBoardCards(pst);
#if 0
    // I used to seed the RNG to the previous board.  This guarantees we will
    // always sample the same river card for a given turn board (no matter the
    // betting state).  The alternative, which we do now, is to sample
    // independently at each betting state.  The danger of this is that we
    // might sample a favorable river card after one turn action, but a less
    // favorable river card after a different turn action.  So the first
    // turn action looks better, but it is really just sampling variance.
    //
    // Having said that, sampling independently seems to lead to lower variance
    // for preflop CV estimates.
    SeedRand(pgbd);
#endif

    // I sample N *distinct* river cards.
    unique_ptr<Card []> rivers(new Card[num_to_sample_]);
    for (unsigned int i = 0; i < num_to_sample_; ++i) {
      unsigned int num_remaining =
	Game::NumCardsInDeck() - num_prev_board_cards - i;
      unsigned int r = RandBetween(0, num_remaining - 1);
      unsigned int num = 0;
      Card river = 0;
      Card max_card = Game::MaxCard();
      for (river = 0; river <= max_card; ++river) {
	if (! InCards(river, prev_board, num_prev_board_cards) &&
	    ! InCards(river, rivers.get(), i)) {
	  if (num == r) {
	    rivers[i] = river;
	    break;
	  }
	  ++num;
	}
      }
      if (river > max_card) {
	fprintf(stderr, "Didn't find river?!?\n");
	exit(-1);
      }
    }
    Card board[5], canon_board[5];
    for (unsigned int i = 0; i < num_prev_board_cards; ++i) {
      board[i] = prev_board[i];
    }
    for (unsigned int i = 0; i < num_to_sample_; ++i) {
      Card river = rivers[i];
      board[num_prev_board_cards] = river;
      CanonicalizeCards(board, nullptr, nst, canon_board, nullptr);
      unsigned int ngbd = BoardTree::LookupBoard(canon_board, nst);
      HandTree hand_tree(nst, ngbd, Game::MaxStreet());
      hand_tree_ = &hand_tree;
      root_bd_st_ = nst;
      root_bd_ = ngbd;
      SetStreetBuckets(nst, ngbd, street_buckets);
      // I can pass unset values for sum_opp_probs and total_card_probs.  I
      // know I will come across an opp choice node before getting to a terminal
      // node.
      double *next_vals = Process(node, 0, opp_probs, 0, nullptr, 
				  street_buckets, action_sequence, nst);

      const CanonicalCards *hands = hand_tree_->Hands(nst, 0);
      unsigned int num_next_hands = hands->NumRaw();
      for (unsigned int nh = 0; nh < num_next_hands; ++nh) {
	const Card *cards = hands->Cards(nh);
	Card hi = cards[0];
	Card lo = cards[1];
	unsigned int enc = hi * (max_card + 1) + lo;
	unsigned int prev_canon = prev_canons[enc];
	// We do not scale by the number of board variants (as we do below).
	// Note that we sample each raw river card with equal probability.
	vals[prev_canon] += next_vals[nh];
      }
      delete [] next_vals;
    }
    hand_tree_ = trunk_hand_tree_;
    root_bd_st_ = 0;
    root_bd_ = 0;
  } else if (nst == 1 && subgame_street_ == kMaxUInt && num_threads_ > 1) {
    for (unsigned int t = 0; t < num_threads_; ++t) {
      // Set stuff
      threads_[t]->SetSplitNode(node);
      threads_[t]->SetSplitOppProbs(opp_probs);
      threads_[t]->SetSplitActionSequence(action_sequence);
      threads_[t]->SetSplitPrevCanons(prev_canons);
    }
    for (unsigned int t = 1; t < num_threads_; ++t) {
      threads_[t]->Run();
    }
    threads_[0]->AfterSplit();
    for (unsigned int t = 1; t < num_threads_; ++t) {
      threads_[t]->Join();
    }
    for (unsigned int t = 0; t < num_threads_; ++t) {
      double *t_vals = threads_[t]->SplitVals();
      for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) {
	vals[i] += t_vals[i];
      }
    }
  } else {
    unsigned int pgbd;
    if (root_bd_st_ == 0) {
      pgbd = plbd;
    } else {
      pgbd = BoardTree::GlobalIndex(root_bd_st_, root_bd_, pst, plbd);
    }
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
    for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      unsigned int nlbd;
      if (root_bd_st_ == 0) {
	nlbd = ngbd;
      } else {
	nlbd = BoardTree::LocalIndex(root_bd_st_, root_bd_, nst, ngbd);
      }
      if (nst == 1) {
	fprintf(stderr, "%s bd %u/%u\n", action_sequence.c_str(), ngbd,
		ngbd_end);
      }

      SetStreetBuckets(nst, ngbd, street_buckets);
      board_counts_[nst] = BoardTree::BoardCount(nst, ngbd);
      // I can pass unset values for sum_opp_probs and total_card_probs.  I
      // know I will come across an opp choice node before getting to a terminal
      // node.
      double *next_vals = Process(node, nlbd, opp_probs, 0, nullptr, 
				  street_buckets, action_sequence, nst);

      unsigned int board_variants = BoardTree::NumVariants(nst, ngbd);
      const CanonicalCards *hands = hand_tree_->Hands(nst, nlbd);
      unsigned int num_next_hands = hands->NumRaw();
      for (unsigned int nh = 0; nh < num_next_hands; ++nh) {
	const Card *cards = hands->Cards(nh);
	Card hi = cards[0];
	Card lo = cards[1];
	unsigned int enc = hi * (max_card + 1) + lo;
	unsigned int prev_canon = prev_canons[enc];
	vals[prev_canon] += board_variants * next_vals[nh];
      }
      delete [] next_vals;
    }
  }
  // Scale down the values of the previous-street canonical hands
  double scale_up;
  if (nst >= sample_st_) {
    unsigned int num_remaining =
      Game::NumCardsInDeck() - Game::NumBoardCards(pst);
    scale_up = ((double)num_remaining) / (double)num_to_sample_;
  } else {
    scale_up = 1.0;
  }
  double scale_down = Game::StreetPermutations(nst);
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    unsigned int prev_hand_variants = pred_hands->NumVariants(ph);
    if (prev_hand_variants > 0) {
      vals[ph] = vals[ph] * scale_up / (scale_down * prev_hand_variants);
    }
  }
  // Copy the canonical hand values to the non-canonical
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      vals[ph] = vals[prev_canons[pred_hands->Canon(ph)]];
    }
  }

  delete [] prev_canons;
  return vals;
}

void SampledBCFRThread::Go(void) {
  time_t start_t = time(NULL);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card = Game::MaxCard();
  unsigned int num;
  if (num_hole_cards == 1) num = max_card + 1;
  else                     num = (max_card + 1) * (max_card + 1);
  double *opp_probs = new double[num];
  for (unsigned int i = 0; i < num; ++i) opp_probs[i] = 1.0;
  const CanonicalCards *hands = hand_tree_->Hands(0, 0);

  double sum_opp_probs;
  double *total_card_probs = new double[num_hole_card_pairs];
  CommonBetResponseCalcs(0, hands, opp_probs, &sum_opp_probs,
			 total_card_probs);
  unsigned int **street_buckets = InitializeStreetBuckets();
  double *vals = Process(betting_tree_->Root(), 0, opp_probs, sum_opp_probs,
			 total_card_probs, street_buckets, "x", 0);
  DeleteStreetBuckets(street_buckets);
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
  printf("P%u EV: %f\n", p_, ev);
  fflush(stdout);

  delete [] opp_probs;
  delete [] vals;

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  fprintf(stderr, "Processing took %.1f seconds\n", diff_sec);
}
