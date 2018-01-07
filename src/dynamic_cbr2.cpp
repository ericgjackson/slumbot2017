// This class performs the same function as DynamicCBR, but is much simpler
// because we leverage the code in VCFR.

#include <stdio.h>
#include <stdlib.h>

#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "dynamic_cbr2.h"
#include "endgame_utils.h"
#include "game.h"
#include "hand_tree.h"
#include "vcfr_state.h"

DynamicCBR2::DynamicCBR2(const CardAbstraction &ca,
			 const BettingAbstraction &ba,
			 const CFRConfig &cc, const Buckets &buckets,
			 unsigned int num_threads) :
  VCFR(ca, ba, cc, buckets, nullptr, num_threads) {
  value_calculation_ = true;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = true;
  }
}

DynamicCBR2::~DynamicCBR2(void) {
}

// Note that we may be working with sumprobs that are specific to a subgame.
// If so, they will be for the subgame rooted at root_bd_st and root_bd.
// So we must map our global board index gbd into a local board index lbd
// whenever we access hand_tree or the probabilities inside sumprobs.
double *DynamicCBR2::Compute(Node *node, unsigned int p, double *opp_probs,
			     unsigned int gbd, HandTree *hand_tree, 
			     unsigned int root_bd_st, unsigned int root_bd,
			     CFRValues *regrets, CFRValues *sumprobs) {
  unsigned int st = node->Street();
  // time_t start_t = time(NULL);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  double *total_card_probs = new double[num_hole_card_pairs];
  unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  unsigned int **street_buckets = AllocateStreetBuckets();
  // Should set this appropriately
  string action_sequence = "x";
  VCFRState state(opp_probs, hand_tree, 0, action_sequence, root_bd,
		  root_bd_st, street_buckets, p, regrets, sumprobs);
  SetStreetBuckets(st, gbd, state);
  double *vals = Process(node, lbd, state, st);
  DeleteStreetBuckets(street_buckets);
  // Temporary?  Make our T values like T values constructed by build_cbrs,
  // by casting to float.
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    vals[i] = (float)vals[i];
  }
  delete [] total_card_probs;
#if 0
  // EVs for our hands are summed over all opponent hole card pairs.  To
  // compute properly normalized EV, need to divide by that number.
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
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
#endif

  FloorCVs(node, opp_probs, hands, vals);
  return vals;
}

// target_p is the player who you want CBR values for.
// Things get confusing in endgame solving.  Suppose we are doing endgame
// solving for P0.  We might say cfr_target_p is 0.  Then I want T-values for
// P1.  So I pass in 1 to Compute(). We'll need the reach probs of P1's
// opponent, who is P0.
double *DynamicCBR2::Compute(Node *node, double **reach_probs,
			     unsigned int gbd, HandTree *hand_tree,
			     unsigned int root_bd_st, unsigned int root_bd,
			     unsigned int target_p, bool cfrs, bool zero_sum,
			     bool current, bool purify_opp,
			     CFRValues *regrets, CFRValues *sumprobs) {
  cfrs_ = cfrs;
  br_current_ = current;
  if (purify_opp) {
    if (current) {
      prob_method_ = ProbMethod::FTL;
    } else {
      prob_method_ = ProbMethod::PURE;
    }
  } else {
    prob_method_ = ProbMethod::REGRET_MATCHING;
  }
  if (zero_sum) {
    double *p0_cvs = Compute(node, 0, reach_probs[1], gbd, hand_tree,
			     root_bd_st, root_bd, regrets, sumprobs);
    double *p1_cvs = Compute(node, 1, reach_probs[0], gbd, hand_tree,
			     root_bd_st, root_bd, regrets, sumprobs);
    unsigned int st = node->Street();
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    // Don't pass in bd.  This is a local hand tree specific to the current
    // board.
    unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
    const CanonicalCards *hands = hand_tree->Hands(st, lbd);
    ZeroSumCVs(p0_cvs, p1_cvs, num_hole_card_pairs, reach_probs, hands);
    if (target_p == 1) {
      delete [] p0_cvs;
      return p1_cvs;
    } else {
      delete [] p1_cvs;
      return p0_cvs;
    }
  } else {
    return Compute(node, target_p, reach_probs[target_p^1], gbd,
		   hand_tree, root_bd_st, root_bd, regrets, sumprobs);
  }
}

