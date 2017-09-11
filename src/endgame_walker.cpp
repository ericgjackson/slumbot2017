// This is a replacement for solve_all_endgames.cpp and endgames.cpp.  It
// should eventually calculate an approximate exploitability number using the
// logic from br.cpp.
//
// Value of P0 best-responder against P1 fixed is:
//   Sum over trunk terminal nodes(...)
//   Sum over endgame terminal nodes(...)
// Likewise, value of P1 best-responder against P0 fixed is:
//   Sum over trunk terminal nodes(...)
//   Sum over endgame terminal nodes(...)
//
// The sums over the trunk terminal nodes cancel each other out.
//
// Doesn't seem to support asymmetric resolving methods yet.
//
// This method may produce an overly optimistic number for the unsafe
// endgame solving method - or any method that utilizes the unsafe method as
// a component, like our combined method.

#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "eg_cfr.h"
#include "endgame_walker.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"

EndgameWalker::EndgameWalker(unsigned int solve_st, unsigned int base_it,
			     const CardAbstraction &base_ca,
			     const CardAbstraction &endgame_ca,
			     const BettingAbstraction &base_ba,
			     const BettingAbstraction &endgame_ba,
			     const CFRConfig &base_cc,
			     const CFRConfig &endgame_cc,
			     const Buckets &base_buckets,
			     const Buckets &endgame_buckets,
			     BettingTree *base_betting_tree,
			     BettingTree *endgame_betting_tree,
			     ResolvingMethod method, bool cfrs, bool zero_sum,
			     unsigned int num_its, unsigned int num_threads) :
  base_card_abstraction_(base_ca), base_betting_abstraction_(base_ba),
  base_cfr_config_(base_cc), base_buckets_(base_buckets) {
  solve_st_ = solve_st;
  base_it_ = base_it;
  base_betting_tree_ = base_betting_tree;
  endgame_betting_tree_ = endgame_betting_tree;
  num_its_ = num_its;
  BoardTree::Create();
  BoardTree::BuildBoardCounts();
  eg_cfr_ = new EGCFR(endgame_ca, base_ca, endgame_ba, base_ba, endgame_cc,
		      base_cc, endgame_buckets, solve_st_, method,
		      cfrs, zero_sum, num_threads);
  // See if we can get by with just these hands
  hand_tree_ = new HandTree(0, 0, solve_st_ - 1);
  unsigned int max_street = Game::MaxStreet();
  street_buckets_ = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (base_buckets_.None(st)) {
      street_buckets_[st] = nullptr;
      continue;
    }
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    street_buckets_[st] = new unsigned int[num_hole_card_pairs];
  }
  sum_all_endgame_norms_ = new double[2];
  sum_sampled_endgame_norms_ = new double[2];
  sum_br_vals_ = new double[2];
}

EndgameWalker::~EndgameWalker(void) {
  delete [] sum_all_endgame_norms_;
  delete [] sum_sampled_endgame_norms_;
  delete [] sum_br_vals_;
  unsigned int max_street = Game::MaxStreet();
  if (street_buckets_) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      delete [] street_buckets_[st];
    }
    delete [] street_buckets_;
  }
  delete hand_tree_;
  delete eg_cfr_;
}

// This assumes we make a single call to SolveSubgame() to compute the
// endgame strategies for both P0 and P1.
void EndgameWalker::ProcessEndgame(Node *base_node, Node *endgame_node, 
				   unsigned int bd, double **reach_probs) {
  bool sample = bd % 2 == 0;
  
  unsigned int max_street = Game::MaxStreet();
  unsigned int board_scale = BoardTree::BoardCount(solve_st_, bd);
  if (solve_st_ < max_street) {
    board_scale *= Game::BoardPermutations(solve_st_ + 1);
  }
  
  HandTree *endgame_hand_tree = new HandTree(solve_st_, bd, max_street);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(solve_st_);
  const CanonicalCards *hands = endgame_hand_tree->Hands(solve_st_, 0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  for (unsigned int p = 0; p <= 1; ++p) {
    double *our_probs = reach_probs[p];
    double *opp_probs = reach_probs[p^1];

    double sum_opp_probs;
    double *total_card_probs = new double[num_hole_card_pairs];
    CommonBetResponseCalcs(solve_st_, hands, opp_probs, &sum_opp_probs,
			   total_card_probs);
    
    double endgame_norm = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double my_sum_opp_probs = sum_opp_probs + opp_probs[enc] -
	total_card_probs[hi] - total_card_probs[lo];
      endgame_norm += our_probs[enc] * board_scale * my_sum_opp_probs;
    }
    sum_all_endgame_norms_[p] += endgame_norm;
    if (sample) sum_sampled_endgame_norms_[p] += endgame_norm;

    delete [] total_card_probs;
  }

  if (sample) {
    unsigned int base_nt = base_node->NonterminalID();
    fprintf(stderr, "NT %u bd %u\n", base_nt, bd);
    eg_cfr_->SolveSubgame(endgame_node, endgame_node, bd, bd, base_nt, base_nt,
			  reach_probs, base_it_, endgame_hand_tree, 1,
			  num_its_);

    BettingTree *subtree = BettingTree::BuildSubtree(endgame_node);

    // Two calls for P1 and P0?
    eg_cfr_->Read(subtree, base_nt, bd, solve_st_, num_its_);
  
    for (unsigned int p = 0; p <= 1; ++p) {
      double *our_probs = reach_probs[p];
      double *vals = eg_cfr_->BRGo(subtree, p, reach_probs, endgame_hand_tree);
      double br_val = 0;
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	const Card *cards = hands->Cards(i);
	unsigned int enc = cards[0] * max_card1 + cards[1];
	// Note: sum of opponent probs is already incorporated into vals[i]
	br_val += vals[i] * our_probs[enc];
      }
      sum_br_vals_[p] += br_val * board_scale;
      delete [] vals;
    }
    delete subtree;
  }
  
  delete endgame_hand_tree;
}

void EndgameWalker::CalculateSuccProbs(Node *node, unsigned int bd,
				       double **reach_probs, 
				       double ****ret_new_reach_probs) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int nt = node->NonterminalID();
  unsigned int pa = node->PlayerActing();
  unsigned int default_succ_index = node->DefaultSuccIndex();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  const CanonicalCards *hands = hand_tree_->Hands(st, bd);

  double ***new_reach_probs = new double **[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    new_reach_probs[s] = new double *[2];
    for (unsigned int p = 0; p <= 1; ++p) {
      if (pa == p) {
	new_reach_probs[s][p] = new double[num_enc];
      } else {
	new_reach_probs[s][p] = reach_probs[p];
      }
    }
  }

  if (! sumprobs_->Ints(pa, st)) {
    fprintf(stderr, "Code currently does not handle double sumprobs\n");
    exit(-1);
  }
  double *current_probs = new double[num_succs];
  int *i_all_sumprobs;
  sumprobs_->Values(pa, st, nt, &i_all_sumprobs);
  if (! base_buckets_.None(st)) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      unsigned int enc = cards[0] * max_card1 + cards[1];
      double reach_prob = reach_probs[pa][enc];
      if (reach_prob == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  new_reach_probs[s][pa][enc] = 0;
	}
      } else {
	unsigned int b = street_buckets_[st][i];
	int *my_sumprobs = i_all_sumprobs + b * num_succs;
	// Assume uniform_ false, no exploration
	RegretsToProbs(my_sumprobs, num_succs, true, false,
		       default_succ_index, 0, 0, nullptr, current_probs);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  new_reach_probs[s][pa][enc] = reach_prob * current_probs[s];
	}
      }
    }
  } else {
    int *i_sumprobs = i_all_sumprobs + bd * num_hole_card_pairs * num_succs;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      unsigned int enc = cards[0] * max_card1 + cards[1];
      double reach_prob = reach_probs[pa][enc];
      if (reach_prob == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  new_reach_probs[s][pa][enc] = 0;
	}
      } else {
	int *my_sumprobs = i_sumprobs + i * num_succs;
	// Assume uniform_ false, no exploration
	RegretsToProbs(my_sumprobs, num_succs, true, false,
		       default_succ_index, 0, 0, nullptr, current_probs);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  new_reach_probs[s][pa][enc] = reach_prob * current_probs[s];
	}
      }
    }
  }
  delete [] current_probs;
  *ret_new_reach_probs = new_reach_probs;
}

void EndgameWalker::InitializeStreetBuckets(unsigned int st, unsigned int bd) {
  if (base_buckets_.None(st)) return;
  Card cards[7];
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_board_cards = Game::NumBoardCards(st);
  const Card *board = BoardTree::Board(st, bd);
  for (unsigned int i = 0; i < num_board_cards; ++i) cards[i + 2] = board[i];
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  const CanonicalCards *hands = hand_tree_->Hands(st, bd);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    unsigned int h;
    if (st == max_street) {
      // Hands on final street were reordered by hand strength, but
      // bucket lookup requires the unordered hole card pair index
      const Card *hole_cards = hands->Cards(i);
      cards[0] = hole_cards[0];
      cards[1] = hole_cards[1];
      unsigned int hcp = HCPIndex(st, cards);
      h = bd * num_hole_card_pairs + hcp;
    } else {
      h = bd * num_hole_card_pairs + i;
    }
    street_buckets_[st][i] = base_buckets_.Bucket(st, h);
  }
}
void EndgameWalker::StreetInitial(Node *base_node, Node *endgame_node,
				  unsigned int pbd, double **reach_probs) {
  unsigned int nst = base_node->Street();
  unsigned int pst = nst - 1;
  unsigned int nbd_begin = BoardTree::SuccBoardBegin(pst, pbd, nst);
  unsigned int nbd_end = BoardTree::SuccBoardEnd(pst, pbd, nst);
  for (unsigned int nbd = nbd_begin; nbd < nbd_end; ++nbd) {
    // Initialize buckets for this street
    InitializeStreetBuckets(nst, nbd);
    Process(base_node, endgame_node, nbd, reach_probs, nst);
  }
}

void EndgameWalker::Process(Node *base_node, Node *endgame_node, 
			    unsigned int bd, double **reach_probs,
			    unsigned int last_st) {
  unsigned int st = base_node->Street();
  if (st == solve_st_ && last_st == solve_st_) {
    ProcessEndgame(base_node, endgame_node, bd, reach_probs);
    return;
  }
  if (base_node->Terminal()) return;
  if (st > last_st) {
    StreetInitial(base_node, endgame_node, bd, reach_probs);
    return;
  }
  unsigned int num_succs = base_node->NumSuccs();
  double ***new_reach_probs;
  CalculateSuccProbs(base_node, bd, reach_probs, &new_reach_probs);
  for (unsigned int s = 0; s < num_succs; ++s) {
    Process(base_node->IthSucc(s), endgame_node->IthSucc(s), bd,
	    new_reach_probs[s], st);
  }
  unsigned int pa = base_node->PlayerActing();
  for (unsigned int s = 0; s < num_succs; ++s) {
    for (unsigned int p = 0; p <= 1; ++p) {
      if (p == pa) delete [] new_reach_probs[s][p];
    }
    delete [] new_reach_probs[s];
  }
  delete [] new_reach_probs;
}

void EndgameWalker::Go(void) {
  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(),
	  base_cfr_config_.CFRConfigName().c_str());

  unsigned int max_street = Game::MaxStreet();
  bool *streets = new bool[max_street + 1];
  bool *compressed_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    streets[st] = st < solve_st_;
    compressed_streets[st] = false;
  }
  sumprobs_.reset(new CFRValues(nullptr, true, streets, base_betting_tree_,
				0, 0, base_card_abstraction_, base_buckets_,
				compressed_streets));
  delete [] compressed_streets;
  delete [] streets;
  sumprobs_->Read(dir, base_it_, base_betting_tree_->Root(),
		  base_betting_tree_->Root()->NonterminalID(), kMaxUInt);

  for (unsigned int p = 0; p <= 1; ++p) {
    sum_all_endgame_norms_[p] = 0;
    sum_sampled_endgame_norms_[p] = 0;
    sum_br_vals_[p] = 0;
  }

  double **reach_probs = new double *[2];
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  const CanonicalCards *hands = hand_tree_->Hands(0, 0);
  for (unsigned int p = 0; p <= 1; ++p) {
    reach_probs[p] = new double[num_enc];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      unsigned int enc = cards[0] * max_card1 + cards[1];
      reach_probs[p][enc] = 1.0;
    }
  }
  Process(base_betting_tree_->Root(), endgame_betting_tree_->Root(), 0,
	  reach_probs, 0);
  for (unsigned int p = 0; p <= 1; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;

  // We normalize by the total number of ways to deal out the cards;
  // i.e., hole cards to the target player, hole cards to the opponent and
  // community cards to the board.
  double total_norm = 1.0;
  unsigned int rem = Game::NumCardsInDeck();
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    total_norm *= (rem * (rem - 1)) / 2;
    rem -= 2;
  }
  for (unsigned int st = 1; st <= max_street; ++st) {
    total_norm *= Game::StreetPermutations(st);
  }

  double gap = 0;
  for (unsigned int p = 0; p <= 1; ++p) {
    // Currently (maybe always?) this will not vary between the players
    double br_scaling = sum_all_endgame_norms_[p] /
      sum_sampled_endgame_norms_[p];
    fprintf(stderr, "br_scaling %f\n", br_scaling);
    gap += sum_br_vals_[p] * br_scaling / total_norm;
    // br_vals[p] = sum_br_vals_[p] / sum_all_endgame_norms_[p];
    // printf("P%u BR: %f\n", p, br_vals[p]);
  }
  // double gap = br_vals[0] + br_vals[1];
  printf("Gap: %f\n", gap);
  printf("Exploitability: %.2f mbb/g\n", ((gap / 2.0) / 2.0) * 1000.0);
  fflush(stdout);
}

