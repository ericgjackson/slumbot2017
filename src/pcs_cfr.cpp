// Not straightforward to unify this code with the CFR class.  Here we are
// always processing a max-street board.
//
// Limit ourselves to small systems that fit in memory entirely.
//
// I could maintain opp probs by hole card pair index instead of by enc.
// Might speed up and simplify things a bit.  But I would need to reimplement
// Showdown() and Fold().
//
// Do I need to weight differently at pre-river terminal nodes?  Seems like
// I am getting good results so maybe I am doing things right.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "pcs_cfr.h"

PCSCFR::PCSCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	       const CFRConfig &cc, const Buckets &buckets,
	       unsigned int num_threads) :
  CFR(), card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(buckets) {
  explore_ = cfr_config_.Explore();
  uniform_ = cfr_config_.Uniform();
  unsigned int max_street = Game::MaxStreet();
  compressed_streets_ = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    compressed_streets_[st] = false;
  }
  const vector<unsigned int> &csv = cfr_config_.CompressedStreets();
  unsigned int num_csv = csv.size();
  for (unsigned int i = 0; i < num_csv; ++i) {
    unsigned int st = csv[i];
    compressed_streets_[st] = true;
  }

  BoardTree::Create();
  BoardTree::BuildBoardCounts();
  BoardTree::BuildPredBoards();
  HandValueTree::Create();

  for (unsigned int st = 0; st <= max_street; ++st) {
    if (buckets_.None(st)) {
      fprintf(stderr, "PCS CFR expects abstraction\n");
      exit(-1);
    }
  }
  
  // Don't support asymmetric yet
  // I don't want to do this for EGPCSCFR.
  betting_tree_ = BettingTree::BuildTree(betting_abstraction_);
  // I don't want to do this for EGPCSCFR.
  hand_tree_ = new HandTree(0, 0, max_street);
  unsigned int num_ms_hole_card_pairs = Game::NumHoleCardPairs(max_street);
  board_buckets_ = new unsigned int[(max_street + 1) * num_ms_hole_card_pairs];

  regrets_.reset(new CFRValues(nullptr, false, nullptr, betting_tree_, 0,
			       0, card_abstraction_, buckets_.NumBuckets(),
			       compressed_streets_));
  // Should check for asymmetric systems
  // Should honor sumprobs_streets_
  sumprobs_.reset(new CFRValues(nullptr, true, nullptr, betting_tree_, 0,
				0, card_abstraction_, buckets_.NumBuckets(),
				compressed_streets_));
  // This will get changed if we do subgame solving
  initial_max_street_board_ = 0;
  end_max_street_boards_ = BoardTree::NumBoards(max_street);
}

PCSCFR::~PCSCFR(void) {
  delete [] board_buckets_;
  delete hand_tree_;
  delete betting_tree_;
}

double *PCSCFR::OurChoice(Node *node, unsigned int msbd, double *opp_probs,
			  double sum_opp_probs, double *total_card_probs) {
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_ms_hole_card_pairs = Game::NumHoleCardPairs(max_street);
  double **succ_vals = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    succ_vals[s] = Process(node->IthSucc(s), msbd, opp_probs, sum_opp_probs,
			   total_card_probs, st);
  }
  double *vals;
  if (num_succs == 1) {
    vals = succ_vals[0];
    succ_vals[0] = nullptr;
  } else {
    vals = new double[num_ms_hole_card_pairs];
    for (unsigned int mshcp = 0; mshcp < num_ms_hole_card_pairs; ++mshcp) {
      vals[mshcp] = 0;
    }

    double *regrets;
    regrets_->Values(p_, st, nt, &regrets);

#if 0
    // Do I want to support unabstracted systems?  Will need to get current
    // street hcp below.
    double *bd_regrets = nullptr;
    if (buckets_.None(st)) {
      unsigned int bd = BoardTree::PredBoard(msbd, st);
      bd_regrets = regrets + bd * num_hole_card_pairs * num_succs;
    }
#endif
  
    unsigned int num_nonterminal_succs = 0;
    bool *nonterminal_succs = new bool[num_succs];
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (node->IthSucc(s)->Terminal()) {
	nonterminal_succs[s] = false;
      } else {
	nonterminal_succs[s] = true;
	++num_nonterminal_succs;
      }
    }
  
    unsigned int default_succ_index = node->DefaultSuccIndex();
    double *current_probs = new double[num_succs];
    for (unsigned int mshcp = 0; mshcp < num_ms_hole_card_pairs; ++mshcp) {
      unsigned int b = board_buckets_[mshcp * (max_street + 1) + st];
      double *bucket_regrets = regrets + b * num_succs;
      RegretsToProbs(bucket_regrets, num_succs, false, uniform_,
		     default_succ_index, explore_, num_nonterminal_succs,
		     nonterminal_succs, current_probs);
      for (unsigned int s = 0; s < num_succs; ++s) {
	vals[mshcp] += succ_vals[s][mshcp] * current_probs[s];
      }
    }
    delete [] current_probs;
    delete [] nonterminal_succs;
    
    for (unsigned int mshcp = 0; mshcp < num_ms_hole_card_pairs; ++mshcp) {
      unsigned int b = board_buckets_[mshcp * (max_street + 1) + st];
      double *bucket_regrets = regrets + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	bucket_regrets[s] += succ_vals[s][mshcp] - vals[mshcp];
      }
    }
  }
	
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_vals[s];
  }
  delete [] succ_vals;
  
  return vals;
}

double *PCSCFR::OppChoice(Node *node, unsigned int msbd, double *opp_probs,
			  double sum_opp_probs, double *total_card_probs) {
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_ms_hole_card_pairs = Game::NumHoleCardPairs(max_street);
  const CanonicalCards *hands = hand_tree_->Hands(max_street, msbd);
  double **succ_opp_probs = new double *[num_succs];

  if (num_succs == 1) {
    succ_opp_probs[0] = opp_probs;
  } else {
    unsigned int pa = node->PlayerActing();
    double *cs_vals;
    regrets_->Values(pa, st, nt, &cs_vals);
    double *sumprobs;
    sumprobs_->Values(pa, st, nt, &sumprobs);

    unsigned int num_hole_cards = Game::NumCardsForStreet(0);
    unsigned int max_card1 = Game::MaxCard() + 1;
    unsigned int num_enc;
    if (num_hole_cards == 1) num_enc = max_card1;
    else                     num_enc = max_card1 * max_card1;
    for (unsigned int s = 0; s < num_succs; ++s) {
      succ_opp_probs[s] = new double[num_enc];
      for (unsigned int i = 0; i < num_enc; ++i) succ_opp_probs[s][i] = 0;
    }

    unsigned int num_nonterminal_succs = 0;
    bool *nonterminal_succs = new bool[num_succs];
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (node->IthSucc(s)->Terminal()) {
	nonterminal_succs[s] = false;
      } else {
	nonterminal_succs[s] = true;
	++num_nonterminal_succs;
      }
    }
  
    unsigned int default_succ_index = node->DefaultSuccIndex();
    double *current_probs = new double[num_succs];
    for (unsigned int mshcp = 0; mshcp < num_ms_hole_card_pairs; ++mshcp) {
      const Card *cards = hands->Cards(mshcp);
      Card hi = cards[0];
      unsigned int enc;
      if (num_hole_cards == 1) {
	enc = hi;
      } else {
	Card lo = cards[1];
	enc = hi * max_card1 + lo;
      }
      double opp_prob = opp_probs[enc];
      if (opp_prob == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  succ_opp_probs[s][enc] = 0;
	}
      } else {
	unsigned int b = board_buckets_[mshcp * (max_street + 1) + st];
	double *bucket_cs_vals = cs_vals + b * num_succs;
	double *bucket_sumprobs = sumprobs + b * num_succs;
	RegretsToProbs(bucket_cs_vals, num_succs, false, uniform_,
		       default_succ_index, explore_, num_nonterminal_succs,
		       nonterminal_succs, current_probs);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  double succ_opp_prob = opp_prob * current_probs[s];
	  succ_opp_probs[s][enc] = succ_opp_prob;
	  bucket_sumprobs[s] += succ_opp_prob;
	}
      }
    }
    delete [] current_probs;
    delete [] nonterminal_succs;
  }

  double *vals = new double[num_ms_hole_card_pairs];
  for (unsigned int mshcp = 0; mshcp < num_ms_hole_card_pairs; ++mshcp) {
    vals[mshcp] = 0;
  }

  double *succ_total_card_probs = new double[num_ms_hole_card_pairs];
  double succ_sum_opp_probs;
  for (unsigned int s = 0; s < num_succs; ++s) {
    CommonBetResponseCalcs(max_street, hands, succ_opp_probs[s],
			   &succ_sum_opp_probs, succ_total_card_probs);
    if (succ_sum_opp_probs == 0) continue;
    double *succ_vals = Process(node->IthSucc(s), msbd, succ_opp_probs[s],
				succ_sum_opp_probs, succ_total_card_probs, st);
    if (succ_vals) {
      for (unsigned int mshcp = 0; mshcp < num_ms_hole_card_pairs; ++mshcp) {
	vals[mshcp] += succ_vals[mshcp];
      }
    }
    delete [] succ_vals;
  }
  delete [] succ_total_card_probs;

  if (num_succs > 1) {
    for (unsigned int s = 0; s < num_succs; ++s) {
      delete [] succ_opp_probs[s];
    }
  }
  delete [] succ_opp_probs;
  
  return vals;
}

double *PCSCFR::Process(Node *node, unsigned int msbd, double *opp_probs,
			double sum_opp_probs, double *total_card_probs,
			unsigned int last_st) {
  unsigned int st = node->Street();
  // fprintf(stderr, "Process st %u sop %f\n", st, sum_opp_probs);
  if (node->Terminal()) {
    double *vals;
    if (node->Showdown()) {
      vals = Showdown(node, hand_tree_->Hands(st, msbd), opp_probs,
		      sum_opp_probs, total_card_probs);
    } else {
      unsigned int bd;
      if (st == Game::MaxStreet()) bd = msbd;
      else                         bd = BoardTree::PredBoard(msbd, st);
      vals = Fold(node, p_, hand_tree_->Hands(st, bd), opp_probs,
		  sum_opp_probs, total_card_probs);
    }
    // Might be nicer if this rescaling was done in Showdown() and Fold().
    unsigned int num_hcp = Game::NumHoleCardPairs(st);
    for (unsigned int i = 0; i < num_hcp; ++i) {
      vals[i] *= board_count_;
    }
    return vals;
  }
  if (node->PlayerActing() == p_) {
    return OurChoice(node, msbd, opp_probs, sum_opp_probs, total_card_probs);
  } else {
    return OppChoice(node, msbd, opp_probs, sum_opp_probs, total_card_probs);
  }
}

void PCSCFR::HalfIteration(BettingTree *betting_tree, unsigned int p,
			   double *opp_probs) {
  p_ = p;
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_ms_boards =
    end_max_street_boards_ - initial_max_street_board_;
  unsigned int msbd = ((it_ - 1) % num_ms_boards) + initial_max_street_board_;
  board_count_ = BoardTree::BoardCount(max_street, msbd);
  unsigned int *num_hole_card_pairs = new unsigned int[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    num_hole_card_pairs[st] = Game::NumHoleCardPairs(st);
  }
  const CanonicalCards *ms_hands = hand_tree_->Hands(max_street, msbd);
  Card cards[7];
  const Card *board = BoardTree::Board(max_street, msbd);
  unsigned int num_board_cards = Game::NumBoardCards(max_street);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    cards[num_hole_cards + i] = board[i];
  }
  unsigned int num_ms_hole_card_pairs = num_hole_card_pairs[max_street];
  for (unsigned int mshcp = 0; mshcp < num_ms_hole_card_pairs; ++mshcp) {
    const Card *hole_cards = ms_hands->Cards(mshcp);
    for (unsigned int i = 0; i < num_hole_cards; ++i) {
      cards[i] = hole_cards[i];
    }
    for (unsigned int st = 0; st < max_street; ++st) {
      unsigned int hcp = HCPIndex(st, cards);
      unsigned int bd = BoardTree::PredBoard(msbd, st);
      unsigned int h = bd * num_hole_card_pairs[st] + hcp;
      board_buckets_[mshcp * (max_street + 1) + st] =
	buckets_.Bucket(st, h);
    }
    unsigned int hcp = HCPIndex(max_street, cards);
    unsigned int h = msbd * num_ms_hole_card_pairs + hcp;
    board_buckets_[mshcp * (max_street + 1) + max_street] =
      buckets_.Bucket(max_street, h);
  }

  unsigned int max_card1 = Game::MaxCard() + 1;
  double *total_card_probs = new double[max_card1];
  double sum_opp_probs;
  CommonBetResponseCalcs(max_street, ms_hands, opp_probs, &sum_opp_probs,
			 total_card_probs);
  double *vals = Process(betting_tree->Root(), msbd, opp_probs, sum_opp_probs,
			 total_card_probs, 0);
  delete [] total_card_probs;

#if 0
  unsigned int num_hcp = num_hole_card_pairs[0];
  unsigned int num_opp_hole_card_pairs = 50 * 49 / 2;
  double sum_vals = 0;
  for (unsigned int i = 0; i < num_hcp; ++i) {
    sum_vals += vals[i] / num_opp_hole_card_pairs;
  }
  double avg_val = sum_vals / num_hcp;
  fprintf(stderr, "P%u avg val %f\n", p, avg_val);
#endif

  delete [] num_hole_card_pairs;
  delete [] vals;
}

void PCSCFR::ReadFromCheckpoint(unsigned int it) {
  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
#if 0
  if (betting_abstraction_.Asymmetric()) {
    char buf2[100];
    sprintf(buf2, ".p%u", target_p_);
    strcat(dir, buf2);
  }
#endif
  regrets_->Read(dir, it, betting_tree_->Root(), "x", kMaxUInt);
  sumprobs_->Read(dir, it, betting_tree_->Root(), "x", kMaxUInt);
}

void PCSCFR::Checkpoint(unsigned int it) {
  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
#if 0
  if (betting_abstraction_.Asymmetric()) {
    char buf2[100];
    sprintf(buf2, ".p%u", target_p_);
    strcat(dir, buf2);
  }
#endif
  regrets_->Write(dir, it, betting_tree_->Root(), "x", kMaxUInt);
  sumprobs_->Write(dir, it, betting_tree_->Root(), "x", kMaxUInt);
}

void PCSCFR::Run(unsigned int start_it, unsigned int end_it) {
  if (start_it == 0) {
    fprintf(stderr, "CFR starts from iteration 1\n");
    exit(-1);
  }
  // Want new version of DeleteOldFiles().
  // DeleteOldFiles(start_it, end_it);

  if (start_it > 1) {
    ReadFromCheckpoint(start_it - 1);
  } else {
    regrets_->AllocateAndClearDoubles(betting_tree_->Root(), kMaxUInt);
    sumprobs_->AllocateAndClearDoubles(betting_tree_->Root(), kMaxUInt);
  }

  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;
  double *opp_probs = new double[num_enc];
  for (unsigned int i = 0; i < num_enc; ++i) opp_probs[i] = 1.0;

  for (it_ = start_it; it_ <= end_it; ++it_) {
    HalfIteration(betting_tree_, 1, opp_probs);
    HalfIteration(betting_tree_, 0, opp_probs);
  }

  delete [] opp_probs;
  
  Checkpoint(end_it);
}

#if 0
// Need base_card_absraction, base_betting_abstraction, base_cfr_config.
// Do I want them as members of PCSCFR?  I guess so.
void EGPCSCFR::WriteSubgame(BettingTree *subtree, unsigned int subtree_nt,
			    unsigned int it) {
  unsigned int root_st = subtree->Root()->Street();
  char dir[500], dir2[500];;
  sprintf(dir2, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(),
	  base_cfr_config_.CFRConfigName().c_str());
#if 0
  if (betting_abstraction_.Asymmetric()) {
    char buf2[100];
    sprintf(buf2, ".p%u", target_p_);
    strcat(dir, buf2);
  }
#endif
  sprintf(dir, "%s/endgames.%s.%s.%s.unsafe.%u", dir2,
	  card_abstraction_.CardAbstractionName().c_str(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str(), root_st);
  Mkdir(dir);
  sumprobs_->Write(dir, it, subtree->Root(), subtree_nt, kMaxUInt);
}

void EGPCSCFR::SolveSubgame(unsigned int root_bd, BettingTree *subtree,
			    double **reach_probs, HandTree *hand_tree,
			    unsigned int base_subtree_nt, unsigned int base_it,
			    unsigned int target_p, unsigned int num_its) {
  unsigned int root_st = subtree->Root()->Street();
  unsigned int max_street = Game::MaxStreet();
  bool *subtree_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    subtree_streets[st] = st >= root_st;
  }
  regrets_.reset(new CFRValues(nullptr, false, subtree_streets, subtree,
			       root_bd, root_st, card_abstraction_,
			       buckets_.NumBuckets(), compressed_streets_));
  regrets_->AllocateAndClearDoubles(subtree->Root(), kMaxUInt);
#if 0
  // Should honor sumprobs_streets_
  // Should handle asymmetric systems
  if (asymmetric_) {
    sumprobs_.reset(new CFRValues(target_p_, target_p_^1, true,
				  subtree_streets, subtree, root_bd, root_st,
				  card_abstraction_, buckets_.NumBuckets(),
				  compressed_streets_));
  }
#endif
  sumprobs_.reset(new CFRValues(nullptr, true, subtree_streets,
				subtree, root_bd, root_st,
				card_abstraction_, buckets_.NumBuckets(),
				compressed_streets_));
  sumprobs_->AllocateAndClearDoubles(subtree->Root(), kMaxUInt);
  delete [] subtree_streets;

  if (root_st == max_street) {
    initial_max_street_board_ = root_bd;
    end_max_street_boards_ = root_bd + 1;
  } else {
    initial_max_street_board_ =
      BoardTree::SuccBoardBegin(root_st, root_bd, max_street);
    end_max_street_boards_ = 
      BoardTree::SuccBoardEnd(root_st, root_bd, max_street);
  }
  
  for (it_ = 1; it_ <= num_its; ++it_) {
    HalfIteration(subtree, true, reach_probs[0]);
    HalfIteration(subtree, false, reach_probs[1]);
  }

  WriteSubgame(subtree, base_subtree_nt, num_its);

  regrets_.reset(nullptr);
  sumprobs_.reset(nullptr);
}

// Use this class when endgame solving
EGPCSCFR::EGPCSCFR(const CardAbstraction &ca, const CardAbstraction &base_ca,
		   const BettingAbstraction &ba,
		   const BettingAbstraction &base_ba, const CFRConfig &cc,
		   const CFRConfig &base_cc, const Buckets &buckets,
		   unsigned int num_threads) :
  PCSCFR(ca, ba, cc, buckets, num_threads), base_card_abstraction_(base_ca),
  base_betting_abstraction_(base_ba), base_cfr_config_(base_cc) {
}
#endif
