// This implementation of CFR for endgames is derived from the code in
// cfrp.cpp.  We handle card isomorphism in the same way, for example.
//
// We represent regrets and sumprobs as doubles rather than ints.  This was
// important for unsafe endgame solving when the sum of the opponent reach
// probs at a subgame was very low.  When we treated regret updates as ints,
// they would all get rounded to zero.  We could potentially also address
// this by increasing the regret and sumprob scaling factors substantially.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "constants.h"
#include "eg_cfr.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "nonterminal_ids.h"
#include "rand.h"
#include "split.h"
#include "vcfr.h"

using namespace std;

EGCFR::EGCFR(const CardAbstraction &ca, const CardAbstraction &base_ca,
	     const BettingAbstraction &ba, const BettingAbstraction &base_ba,
	     const CFRConfig &cc, const CFRConfig &base_cc,
	     const Buckets &buckets, unsigned int subtree_st,
	     ResolvingMethod method, bool cfrs, bool zero_sum,
	     unsigned int num_threads) :
  VCFR(ca, ba, cc, buckets, nullptr, num_threads),
  base_card_abstraction_(base_ca), base_betting_abstraction_(base_ba),
  base_cfr_config_(base_cc) {
  root_bd_st_ = subtree_st;
  method_ = method;
  cfrs_ = cfrs;
  zero_sum_ = zero_sum;

  HandValueTree::Create();
  BoardTree::Create();
  it_ = 0;
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  cfrd_regrets_ = nullptr;
  maxmargin_regrets_ = nullptr;
  combined_regrets_ = nullptr;
  symmetric_combined_regrets_ = nullptr;
  // sum_target_probs_ = nullptr;

  if (method_ == ResolvingMethod::CFRD) {
    cfrd_regrets_ = new double[num_hole_card_pairs * 2];
  } else if (method_ == ResolvingMethod::MAXMARGIN) {
    maxmargin_regrets_ = new double[num_hole_card_pairs];
  } else if (method_ == ResolvingMethod::COMBINED) {
    combined_regrets_ = new double[num_hole_card_pairs * 2];
    // sum_target_probs_ = new double[num_hole_card_pairs * 2];
  }

  hand_tree_ = nullptr;
  sumprobs_ = nullptr;
  regrets_ = nullptr;
}

// Assume regrets and sumprobs were deleted already
EGCFR::~EGCFR(void) {
  delete [] cfrd_regrets_;
  delete [] maxmargin_regrets_;
  delete [] combined_regrets_;
  if (symmetric_combined_regrets_) {
    for (unsigned int p = 0; p <= 1; ++p) {
      delete [] symmetric_combined_regrets_[p];
    }
    delete [] symmetric_combined_regrets_;
  }
  // delete [] sum_target_probs_;
}

double *EGCFR::HalfIteration(BettingTree *subtree, unsigned int p,
			     double *opp_probs,
			     unsigned int **street_buckets) {
  p_ = p;
  Node *subtree_root = subtree->Root();
  unsigned int max_card = Game::MaxCard();
  double *total_card_probs = new double[max_card + 1];
  double sum_opp_probs;
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  CommonBetResponseCalcs(subtree_root->Street(), hands, opp_probs,
			 &sum_opp_probs, total_card_probs);
  // There is nothing to be done in endgame solving if no opponent
  // hands reach the subtree with non-zero probability.  Can just return.
  // Strategy learned will be default action everywhere (always check/call).
  if (sum_opp_probs == 0) {
    delete [] total_card_probs;
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
    double *vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
    return vals;
  }
  // We're not passing in the right action sequence but it doesn't matter
  double *vals = Process(subtree_root, 0, opp_probs, sum_opp_probs,
			 total_card_probs, street_buckets, "",
			 subtree_root->Street());

  delete [] total_card_probs;
  return vals;
}

// Simulate dummy root node with two succs.  Succ 0 corresponds to playing to
// the subgame.  Succ 1 corresponds to taking the T value.
// Use "villain" to mean the player who is not the target player.
void EGCFR::CFRDHalfIteration(BettingTree *subtree, unsigned int p,
			      double *target_reach_probs,
			      double *opp_cvs, unsigned int **street_buckets) {
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;
  double *villain_probs = new double[num_enc];
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  bool nonneg = nn_regrets_ && regret_floors_[root_bd_st_] >= 0;
  double *probs = new double[2];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double *regrets = &cfrd_regrets_[i * 2];
    RegretsToProbs(regrets, 2, nonneg, uniform_, 0, 0, 0, nullptr, probs);
    villain_probs[enc] = probs[0];
  }
  delete [] probs;
  
  if (p == target_p_) {
    double *vals = HalfIteration(subtree, p, villain_probs, street_buckets);
    delete [] vals;
  } else {
    // Opponent phase.  The target player plays his fixed range to the
    // subgame.
    double *vals = HalfIteration(subtree, p, target_reach_probs,
				 street_buckets);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      double *regrets = &cfrd_regrets_[i * 2];
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      unsigned int enc;
      if (num_hole_cards == 1) {
	enc = hi;
      } else {
	Card lo = cards[1];
	enc = hi * max_card1 + lo;
      }
      double t_value = opp_cvs[i];
      double val = villain_probs[enc] * vals[i] +
	(1.0 - villain_probs[enc]) * t_value;
      double delta0 = vals[i] - val;
      double delta1 = t_value - val;
      regrets[0] += delta0;
      regrets[1] += delta1;
      if (nonneg) {
	if (regrets[0] < 0) regrets[0] = 0;
	if (regrets[1] < 0) regrets[1] = 0;
      }
    }
    delete [] vals;
  }
  
  delete [] villain_probs;
}

#if 0
// Attempt at a different method for combine unsafe, CFR-D and uniform
// ranges.
void EGCFR::CombinedHalfIteration(BettingTree *subtree, unsigned int p,
				  double **reach_probs,
				  unsigned int **street_buckets) {
  double *cfrd_probs = new double[num_enc];
  double *unsafe_probs = new double[num_enc];
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  bool nonneg = nn_regrets_ && regret_floors_[root_bd_st_] >= 0;
  double probs[2];
  double sum_unsafe_probs = 0, sum_cfrd_probs = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    unsafe_probs[enc] = reach_probs[target_p_^1][enc];
    sum_unsafe_probs += unsafe_probs[enc];
    double *regrets = &combined_regrets_[i * 2];
    RegretsToProbs(regrets, 2, nonneg, uniform_, 0, 0, 0, nullptr, probs);
    cfrd_probs[enc] = probs[0];
    sum_cfrd_probs += cfrd_probs[enc];
  }
  // Normalize the three probability distributions so that the probabilities
  // in each one sum to 1.0.
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    unsafe_probs[enc] /= sum_unsafe_probs;
    cfrd_probs[enc] /= sum_cfrd_probs;
  }
  double *villain_probs = new double[num_enc];
  double cfrd_wt = 0.4, uniform_wt = 0.2;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    // Note: normalized uniform probability is just 1 / num_hole_card_pairs.
    villain_probs[enc] = unsafe_probs[enc] +
      uniform_wt * (1.0 / num_hole_card_pairs);
    if (villain_probs[enc] > 1.0) villain_probs[enc] = 1.0;
    double rem = 1.0 - villain_probs[enc];
    villain_probs[enc] += rem * cfrd_wt * cfrd_probs[enc];
    if (villain_probs[enc] > 1.0) villain_probs[enc] = 1.0;
  }
  delete [] unsafe_probs;
  delete [] cfrd_probs;
  
}
#endif

#if 0
void EGCFR::SymmetricCombinedHalfIteration(BettingTree *subtree,
					   unsigned int p,
					   double **reach_probs,
					   double **opp_cvs,
					   unsigned int **street_buckets) {
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;
  double **probs = new double *[2];
  double *sum_probs = new double[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    probs[p] = new double[num_enc];
    sum_probs[p] = 0;
  }
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  bool nonneg = nn_regrets_ && regret_floors_[root_bd_st_] >= 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    for (unsigned int p = 0; p <= 1; ++p) {
      probs[p][enc] = reach_probs[p][enc];
      sum_probs[p] += probs[p][enc];
    }
  }
  double sum_to_add = 0;
  double *cfrd_probs = new double[2];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card lo, hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double rem = 1.0 - villain_probs[enc];
    if (rem > 0) {
      double *regrets = &combined_regrets_[i * 2];
      RegretsToProbs(regrets, 2, nonneg, uniform_, 0, 0, 0, nullptr,
		     probs);
      sum_to_add += rem * probs[0];
    }
  }
  
  double scale = 1.0;
  // 0.2 best so far
  double cfrd_cap = 0.2;
}
#endif

// Simulate dummy root node with two succs.  Succ 0 corresponds to playing to
// the subgame.  Succ 1 corresponds to taking the T value.
// Use "villain" to mean the player who is not the target player.
void EGCFR::CombinedHalfIteration(BettingTree *subtree, unsigned int p,
				  double **reach_probs, double *opp_cvs,
				  unsigned int **street_buckets) {
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;
  double *villain_probs = new double[num_enc];
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  bool nonneg = nn_regrets_ && regret_floors_[root_bd_st_] >= 0;
  double sum_villain_reach_probs = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    villain_probs[enc] = reach_probs[target_p_^1][enc];
    sum_villain_reach_probs += villain_probs[enc];
  }
  double sum_to_add = 0;
  double *probs = new double[2];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card lo, hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double rem = 1.0 - villain_probs[enc];
    if (rem > 0) {
      double *regrets = &combined_regrets_[i * 2];
      RegretsToProbs(regrets, 2, nonneg, uniform_, 0, 0, 0, nullptr,
		     probs);
      sum_to_add += rem * probs[0];
    }
  }
  
  double scale = 1.0;
  // 0.2 best so far
  double cfrd_cap = 0.2;
#if 0
  double gap_factor = avg_t_values_ / subtree->Root()->PotSize();
  if (gap_factor > 0.02) {
    cfrd_cap = 1.0;
  }
#endif
  if (sum_to_add > sum_villain_reach_probs * cfrd_cap) {
    scale = (sum_villain_reach_probs * cfrd_cap) / sum_to_add;
  }
#if 0
  if (sum_villain_reach_probs < 10.0) {
    if (sum_to_add > sum_villain_reach_probs * 0.4) {
      scale = (sum_villain_reach_probs  * 0.4) / sum_to_add;
    }
  } else {
    if (sum_to_add > sum_villain_reach_probs * 0.1) {
      scale = (sum_villain_reach_probs * 0.1) / sum_to_add;
    }
  }
#endif
#if 0
  if (sum_to_add >= 4.0) scale = 4.0 / sum_to_add;
#endif
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double rem = 1.0 - villain_probs[enc];
    if (rem > 0) {
      double *regrets = &combined_regrets_[i * 2];
      RegretsToProbs(regrets, 2, nonneg, uniform_, 0, 0, 0, nullptr,
		     probs);
      villain_probs[enc] += rem * probs[0] * scale;
    }
  }
  delete [] probs;

  double prob_mass = sum_villain_reach_probs + scale * sum_to_add;
  // Best: 0.1
  double uniform_add = 0.1 * prob_mass / num_hole_card_pairs;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    villain_probs[enc] += uniform_add;
    if (villain_probs[enc] > 1.0) villain_probs[enc] = 1.0;
  }
  
  if (p == target_p_) {
    double *vals = HalfIteration(subtree, p, villain_probs, street_buckets);
    delete [] vals;
  } else {
    // Opponent phase.  The target player plays his fixed range to the
    // subgame.
    double *target_reach_probs = reach_probs[target_p_];
    double *vals = HalfIteration(subtree, p, target_reach_probs,
				 street_buckets);
#if 0
    if (it_ == 200 && target_p_ == 0) {
      for (unsigned int i = 0; i < num_hole_card_pairs; i += 100) {
	fprintf(stderr, "p_ %u vals[%u] %f\n", p_, i, vals[i]);
      }
    }
#endif
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      double *regrets = &combined_regrets_[i * 2];
      const Card *cards = hands->Cards(i);
      Card lo, hi = cards[0];
      unsigned int enc;
      if (num_hole_cards == 1) {
	enc = hi;
      } else {
	lo = cards[1];
	enc = hi * max_card1 + lo;
      }
      double t_value = opp_cvs[i];
#if 0
      // Temporary
      // Add random noise to T value
      t_value += (RandZeroToOne() - 0.5) * subtree->Root()->PotSize() * 0.05 *
	sum_target_probs_[i];
#endif
      double val = villain_probs[enc] * vals[i] +
	(1.0 - villain_probs[enc]) * t_value;
      double delta0 = vals[i] - val;
      double delta1 = t_value - val;
      regrets[0] += delta0;
      regrets[1] += delta1;
      if (nonneg) {
	if (regrets[0] < 0) regrets[0] = 0;
	if (regrets[1] < 0) regrets[1] = 0;
      }
    }
    delete [] vals;
  }
  
  delete [] villain_probs;
}

// Multiple issues.
// 1) Opp probs across all hands sum to 1.0.  Normally opp probs sum to,
// e.g., 1326.0.  Does it matter?  "T" values were computed with opp probs
// computed the normal way, right?  Hang on, if we are doing a P1 system,
// issue arises during P2 phase.  P2 "T" values were calculated relative to
// P1 hands that reach, right?
void EGCFR::MaxMarginHalfIteration(BettingTree *subtree, unsigned int p,
				   double **reach_probs, double *opp_cvs,
				   unsigned int **street_buckets) {
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;
  double *opp_probs = new double[num_enc];
  for (unsigned int i = 0; i < num_enc; ++i) opp_probs[i] = 0;
  double sum_regrets = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    double r = maxmargin_regrets_[i];
    if (r > 0) sum_regrets += r;
  }
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  if (sum_regrets == 0) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      unsigned int enc;
      if (num_hole_cards == 1) {
	enc = hi;
      } else {
	Card lo = cards[1];
	enc = hi * max_card1 + lo;
      }
      opp_probs[enc] = 1.0 / (double)num_hole_card_pairs;
    }      
  } else {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      double r = maxmargin_regrets_[i];
      if (r <= 0) continue;
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      unsigned int enc;
      if (num_hole_cards == 1) {
	enc = hi;
      } else {
	Card lo = cards[1];
	enc = hi * max_card1 + lo;
      }
      opp_probs[enc] = r / sum_regrets;
    }
  }
  if (p == target_p_) {
    double *vals = HalfIteration(subtree, p, opp_probs, street_buckets);
    delete [] vals;
  } else {
    double *opp_reach_probs = reach_probs[p^1];
    double *vals = HalfIteration(subtree, p, opp_reach_probs, street_buckets);
    // Offset CVs by "T" values
    double val = 0, sum_probs = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      unsigned int enc;
      if (num_hole_cards == 1) {
	enc = hi;
      } else {
	Card lo = cards[1];
	enc = hi * max_card1 + lo;
      }
      double prob = opp_probs[enc];
      val += prob * (vals[i] - opp_cvs[i]);
      sum_probs += prob;
    }
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      double ri = (vals[i] - opp_cvs[i]) - val;
      maxmargin_regrets_[i] += ri;
    }
    delete [] vals;
  }
  delete [] opp_probs;
}

void FloorCVs(Node *subtree_root, double *opp_reach_probs,
	      const CanonicalCards *hands, double *cvs) {
  unsigned int st = subtree_root->Street();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int maxcard1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    double sum_opp_reach_probs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
      sum_opp_reach_probs += opp_reach_probs[opp_enc];
    }
    double our_norm_cv = cvs[i] / sum_opp_reach_probs;
    if (our_norm_cv < -((double)subtree_root->PotSize()) / 2.0) {
      cvs[i] = (-((double)subtree_root->PotSize()) / 2.0) * sum_opp_reach_probs;
    }
  }
  
}

// Load player p's CVs
double *EGCFR::LoadCVs(Node *subtree_root, const string &action_sequence,
		       unsigned int gbd, unsigned int base_it, unsigned int p,
		       double **reach_probs, const CanonicalCards *hands,
		       bool card_level) {
  bool base_bucketed = false;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    const string &bk = base_card_abstraction_.Bucketing(st);
    if (bk != "none") {
      base_bucketed = true;
      break;
    }
  }
  if (! card_level && ! base_bucketed) {
    fprintf(stderr, "Can't use bucket-level CVs if base had no card "
	    "abstraction\n");
    exit(-1);
  }

  char dir[500], buf[500];
  // This assumes two players
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s/%s.%u.p%u/%s",
	  Files::NewCFRBase(), Game::GameName().c_str(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(), 
	  base_cfr_config_.CFRConfigName().c_str(),
	  card_level ? (cfrs_ ? "cfrs" : "cbrs") : (cfrs_ ? "bcfrs" : "bcbrs"),
	  base_it, p, action_sequence.c_str());
  sprintf(buf, "%s/vals.%u", dir, gbd);
	  
  Reader reader(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  double *cvs = new double[num_hole_card_pairs];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    cvs[i] = reader.ReadFloatOrDie();
  }

  FloorCVs(subtree_root, reach_probs[p^1], hands, cvs);
  return cvs;
}

void CalculateMeanCVs(double *p0_cvs, double *p1_cvs,
		      unsigned int num_hole_card_pairs, double **reach_probs,
		      const CanonicalCards *hands, double *p0_mean_cv,
		      double *p1_mean_cv) {
  unsigned int maxcard1 = Game::MaxCard() + 1;
  double sum_p0_cvs = 0, sum_p1_cvs = 0, sum_joint_probs = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    unsigned int our_enc = our_hi * maxcard1 + our_lo;
    double sum_p0_opp_probs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
      sum_p0_opp_probs += reach_probs[0][opp_enc];
    }
    double p0_prob = reach_probs[0][our_enc];
    double p1_prob = reach_probs[1][our_enc];
    sum_p0_cvs += p0_cvs[i] * p0_prob;
    sum_p1_cvs += p1_cvs[i] * p1_prob;
    sum_joint_probs += p1_prob * sum_p0_opp_probs;
#if 0
    if (p0_cvs[i] > 10000 || p0_cvs[i] < -10000) {
      fprintf(stderr, "OOB p0 cv i %u cv %f\n", i, p0_cvs[i]);
      exit(-1);
    }
    if (p1_cvs[i] > 10000 || p1_cvs[i] < -10000) {
      fprintf(stderr, "OOB p1 cv i %u cv %f\n", i, p1_cvs[i]);
      exit(-1);
    }
#endif
  }
  *p0_mean_cv = sum_p0_cvs / sum_joint_probs;
  *p1_mean_cv = sum_p1_cvs / sum_joint_probs;
}

void ZeroSumCVs(double *p0_cvs, double *p1_cvs,
		unsigned int num_hole_card_pairs, double **reach_probs,
		const CanonicalCards *hands) {
  double p0_mean_cv, p1_mean_cv;
  CalculateMeanCVs(p0_cvs, p1_cvs, num_hole_card_pairs, reach_probs, hands,
		   &p0_mean_cv, &p1_mean_cv);
  fprintf(stderr, "Mean CVs: %f, %f\n", p0_mean_cv, p1_mean_cv);

  double avg = (p0_mean_cv + p1_mean_cv) / 2.0;
  double adj = -avg;
  unsigned int maxcard1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    double sum_p0_opp_probs = 0, sum_p1_opp_probs = 0;    
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
      sum_p0_opp_probs += reach_probs[0][opp_enc];
      sum_p1_opp_probs += reach_probs[1][opp_enc];
    }
    p0_cvs[i] += adj * sum_p1_opp_probs;
    p1_cvs[i] += adj * sum_p0_opp_probs;
  }

  // I can take this out
  double adj_p0_mean_cv, adj_p1_mean_cv;
  CalculateMeanCVs(p0_cvs, p1_cvs, num_hole_card_pairs, reach_probs, hands,
		   &adj_p0_mean_cv, &adj_p1_mean_cv);
  fprintf(stderr, "Adj mean CVs: P0 %f, P1 %f\n", adj_p0_mean_cv,
	  adj_p1_mean_cv);
  
}

double *EGCFR::LoadZeroSumCVs(Node *subtree_root,
			      const string &action_sequence, unsigned int gbd,
			      unsigned int target_p, unsigned int base_it,
			      double **reach_probs,
			      const CanonicalCards *hands, bool card_level) {
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  double *p0_cvs = LoadCVs(subtree_root, action_sequence, gbd, base_it, 0,
			   reach_probs, hands, card_level);
  double *p1_cvs = LoadCVs(subtree_root, action_sequence, gbd, base_it, 1,
			   reach_probs, hands, card_level);

  ZeroSumCVs(p0_cvs, p1_cvs, num_hole_card_pairs, reach_probs, hands);

  if (target_p == 0) {
    delete [] p0_cvs;
    return p1_cvs;
  } else {
    delete [] p1_cvs;
    return p0_cvs;
  }
}

static Node *FindCorrespondingNode(Node *node1, Node *node2, Node *target1) {
  if (node1 == target1) {
    return node2;
  }
  if (node1->Terminal()) return nullptr;
  unsigned int num_succs1 = node1->NumSuccs();
  unsigned int num_succs2 = node2->NumSuccs();
  if (num_succs1 != num_succs2) {
    fprintf(stderr, "num_succs mismatch\n");
    exit(-1);
  }
  for (unsigned int s = 0; s < num_succs1; ++s) {
    Node *ret = FindCorrespondingNode(node1->IthSucc(s), node2->IthSucc(s),
				      target1);
    if (ret) return ret;
  }
  return nullptr;
}

double *EGCFR::LoadOppCVs(Node *solve_root, const string &action_sequence,
			  unsigned int bd, unsigned int target_p,
			  unsigned int base_it, double **reach_probs,
			  const CanonicalCards *hands, bool card_level) {
  if (method_ == ResolvingMethod::CFRD ||
      method_ == ResolvingMethod::MAXMARGIN ||
      method_ == ResolvingMethod::COMBINED) {
    if (zero_sum_) {
      return LoadZeroSumCVs(solve_root, action_sequence, bd, target_p, base_it,
			    reach_probs, hands, card_level);
    } else {
      return LoadCVs(solve_root, action_sequence, bd, base_it,
		     target_p^1, reach_probs, hands, card_level);
    }
  } else {
    return nullptr;
  }
}

// We allow for a separate "solve" subtree and "target" subtree.  We want to
// compute a strategy for the target subtree.  But to do do that we may
// "back up" and solve a larger enclosing subtree - the "solve" subtree.  We
// only save the computed strategy for the target subtree.
// The subtree is defined by the node and the board.
void EGCFR::SolveSubgame(BettingTree *subtree, unsigned int solve_bd,
			 double **reach_probs, HandTree *hand_tree,
			 double *opp_cvs, unsigned int target_p,
			 bool both_players, unsigned int num_its) {
  root_bd_ = solve_bd;
  target_p_ = target_p;
  hand_tree_ = hand_tree;
  // DeleteOldFiles(gbd);

  unsigned int max_street = Game::MaxStreet();
  bool *subtree_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    subtree_streets[st] = st >= root_bd_st_;
  }
  regrets_.reset(new CFRValues(nullptr, false, subtree_streets,
			       subtree, root_bd_, root_bd_st_,
			       card_abstraction_, buckets_,
			       compressed_streets_));
  
  regrets_->AllocateAndClearDoubles(subtree->Root(), kMaxUInt);

  // Should honor sumprobs_streets_

  // Unsafe endgame solving always produces sumprobs for both players.
  if (method_ == ResolvingMethod::UNSAFE) both_players = true;

  if (both_players) {
    sumprobs_.reset(new CFRValues(nullptr, true, subtree_streets,
				  subtree, root_bd_, root_bd_st_,
				  card_abstraction_, buckets_,
				  compressed_streets_));
  } else {
    unsigned int num_players = Game::NumPlayers();
    unique_ptr<bool []> players(new bool[num_players]);
    for (unsigned int p = 0; p < num_players; ++p) {
      players[p] = p == target_p_;
    }
    sumprobs_.reset(new CFRValues(players.get(), true, subtree_streets, subtree,
				  root_bd_, root_bd_st_, card_abstraction_,
				  buckets_, compressed_streets_));
  }
  sumprobs_->AllocateAndClearDoubles(subtree->Root(), kMaxUInt);
  delete [] subtree_streets;

  unsigned int **street_buckets = InitializeStreetBuckets();
  if (! buckets_.None(root_bd_st_)) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
    if (max_street == root_bd_st_) {
      const Card *board = BoardTree::Board(root_bd_st_, root_bd_);
      unsigned int num_board_cards = Game::NumBoardCards(root_bd_st_);
      Card cards[7];
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	cards[i+2] = board[i];
      }
      const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	const Card *hole_cards = hands->Cards(i);
	cards[0] = hole_cards[0];
	cards[1] = hole_cards[1];
	unsigned int hcp = HCPIndex(root_bd_st_, cards);
	unsigned int h = root_bd_ * num_hole_card_pairs + hcp;
	street_buckets[root_bd_st_][i] = buckets_.Bucket(root_bd_st_, h);
      }
    } else {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	unsigned int h = root_bd_ * num_hole_card_pairs + i;
	street_buckets[root_bd_st_][i] = buckets_.Bucket(root_bd_st_, h);
      }
    }
  }

#if 0
  if (1) {
    double sum_our_reach_probs = 0;
    double sum_opp_reach_probs = 0;
    double *our_reach_probs = reach_probs[target_p_];
    double *opp_reach_probs = reach_probs[target_p_^1];
    unsigned int maxcard1 = Game::MaxCard() + 1;
    const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      unsigned int enc = cards[0] * maxcard1 + cards[1];
      double our_prob = our_reach_probs[enc];
      sum_our_reach_probs += our_prob;
      sum_opp_reach_probs += opp_reach_probs[enc];
    }
    fprintf(stderr, "Target P%u sum our reach probs: %f\n", target_p_,
	    sum_our_reach_probs);
    fprintf(stderr, "Target P%u sum opp reach probs: %f\n", target_p_,
	    sum_opp_reach_probs);
  }
#endif

  if (method_ == ResolvingMethod::UNSAFE) {
    double sums[2];
    sums[0] = 0;
    sums[1] = 0;
    unsigned int nums[2];
    nums[0] = 0;
    nums[1] = 0;
    unsigned int maxcard1 = Game::MaxCard() + 1;
    const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
    for (unsigned int p = 0; p <= 1; ++p) {
      double *p_reach_probs = reach_probs[p];
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	const Card *cards = hands->Cards(i);
	unsigned int enc = cards[0] * maxcard1 + cards[1];
	double prob = p_reach_probs[enc];
	if (prob > 0.002) {
	  ++nums[p];
	}
	sums[p] += prob;
      }
      fprintf(stderr, "%s has %u/%u >0.002 probs; prob sum %f\n",
	      p ? "P1" : "P2", nums[p], num_hole_card_pairs, sums[p]);
    }
  }

  if (method_ == ResolvingMethod::CFRD) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      cfrd_regrets_[i * 2] = 0;
      cfrd_regrets_[i * 2 + 1] = 0;
    }
  } else if (method_ == ResolvingMethod::MAXMARGIN) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      maxmargin_regrets_[i] = 0;
    }
  } else if (method_ == ResolvingMethod::COMBINED) {
    // Calculate the sum of target player reach probs for each opponent hand
    // (taking card replacement effects into account).  Will allows us to
    // normalize opp_cvs_.
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
#if 0
    const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
    unsigned int maxcard1 = Game::MaxCard() + 1;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *opp_cards = hands->Cards(i);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      sum_target_probs_[i] = 0;
      for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
	const Card *target_cards = hands->Cards(j);
	Card target_hi = target_cards[0];
	Card target_lo = target_cards[1];
	if (target_hi == opp_hi || target_hi == opp_lo || target_lo == opp_hi ||
	    target_lo == opp_lo) {
	  continue;
	}
	unsigned int target_enc = target_hi * maxcard1 + target_lo;
	sum_target_probs_[i] += reach_probs[target_p_][target_enc];
      }
    }
#endif
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      combined_regrets_[i * 2] = 0;
      combined_regrets_[i * 2 + 1] = 0;
    }
  }

  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = false;
  }
  value_calculation_ = false;
  double *vals;
  for (it_ = 1; it_ <= num_its; ++it_) {
    // fprintf(stderr, "It %i\n", it_);
    if (method_ == ResolvingMethod::MAXMARGIN) {
      MaxMarginHalfIteration(subtree, 1, reach_probs, opp_cvs, street_buckets);
      MaxMarginHalfIteration(subtree, 0, reach_probs, opp_cvs, street_buckets);
    } else if (method_ == ResolvingMethod::CFRD) {
      CFRDHalfIteration(subtree, 1, reach_probs[target_p_], opp_cvs,
			street_buckets);
      CFRDHalfIteration(subtree, 0, reach_probs[target_p_], opp_cvs,
			street_buckets);
    } else if (method_ == ResolvingMethod::UNSAFE) {
      vals = HalfIteration(subtree, 1, reach_probs[0], street_buckets);
      delete [] vals;
      vals = HalfIteration(subtree, 0, reach_probs[1], street_buckets);
      delete [] vals;
    } else if (method_ == ResolvingMethod::COMBINED) {
      CombinedHalfIteration(subtree, 1, reach_probs, opp_cvs, street_buckets);
      CombinedHalfIteration(subtree, 0, reach_probs, opp_cvs, street_buckets);
    }
  }
  DeleteStreetBuckets(street_buckets);
  
#if 0
  if (target_p_ == 0) {
    double *d_all_sumprobs;
    sumprobs_->Values(0, 1, 0, &d_all_sumprobs);
    for (unsigned int i = 0; i < 1000; i += 100) {
      fprintf(stderr, "P0 i %u %f/%f\n", i, d_all_sumprobs[i*2],
	      d_all_sumprobs[i*2+1]);
    }
  }
#endif
}

const char *ResolvingMethodName(ResolvingMethod method) {
  if (method == ResolvingMethod::UNSAFE) {
    return "unsafe";
  } else if (method == ResolvingMethod::CFRD) {
    return "cfrd";
  } else if (method == ResolvingMethod::MAXMARGIN) {
    return "maxmargin";
  } else if (method == ResolvingMethod::COMBINED) {
    return "combined";
  } else {
    fprintf(stderr, "Unknown resolving method\n");
    exit(-1);
  }
}

void EGCFR::Write(BettingTree *subtree, Node *solve_root, Node *target_root,
		  const string &action_sequence, unsigned int num_its,
		  unsigned int target_bd) {
  // I need the node that corresponds to target node within subtree
  Node *subtree_target = FindCorrespondingNode(solve_root, subtree->Root(),
					       target_root);
  if (target_root == nullptr) {
    fprintf(stderr, "Could not find target node within subtree\n");
    exit(-1);
  }
  Write(subtree_target, action_sequence, num_its, target_root->Street(),
	target_bd);
}

void EGCFR::Write(Node *target_root, const string &action_sequence,
		  unsigned int it, unsigned int target_st,
		  unsigned int target_bd) {
  char dir[500], dir2[500];
  sprintf(dir2, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(),
	  base_cfr_config_.CFRConfigName().c_str());
  if (base_betting_abstraction_.Asymmetric()) {
    if (target_p_) strcat(dir2, ".p1");
    else           strcat(dir2, ".p2");
  }
  sprintf(dir, "%s/endgames.%s.%s.%s.%s.%u.%u", dir2,
	  card_abstraction_.CardAbstractionName().c_str(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str(), ResolvingMethodName(method_),
	  root_bd_st_, target_st);
  Mkdir(dir);
  if (target_st > root_bd_st_) {
    fprintf(stderr, "Stopped supporting target_st > root_bd_st_\n");
    exit(-1);
  } else {
    sumprobs_->Write(dir, it, target_root, action_sequence, kMaxUInt);
  }
}

void EGCFR::Read(BettingTree *subtree, const string &action_sequence,
		 unsigned int subtree_bd, unsigned int target_st,
		 bool both_players, unsigned int it) {
  unsigned int max_street = Game::MaxStreet();
  bool *subtree_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    subtree_streets[st] = st >= root_bd_st_;
  }

  // Should honor sumprobs_streets_
  // Unsafe endgame solving always produces sumprobs for both players.
  if (method_ == ResolvingMethod::UNSAFE) both_players = true;
  if (both_players) {
    sumprobs_.reset(new CFRValues(nullptr, true, subtree_streets, subtree,
				  subtree_bd, root_bd_st_,
				  card_abstraction_, buckets_,
				  compressed_streets_));
  } else {
    unsigned int num_players = Game::NumPlayers();
    unique_ptr<bool []> players(new bool[num_players]);
    for (unsigned int p = 0; p < num_players; ++p) {
      players[p] = p == target_p_;
    }
    sumprobs_.reset(new CFRValues(players.get(), true, subtree_streets,
				  subtree, subtree_bd, root_bd_st_,
				  card_abstraction_, buckets_,
				  compressed_streets_));
  }
  delete [] subtree_streets;

  char dir[500], dir2[500];;
  sprintf(dir2, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(),
	  base_cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    if (target_p_) strcat(dir2, ".p1");
    else           strcat(dir2, ".p2");
  }
  sprintf(dir, "%s/endgames.%s.%s.%s.%s.%u.%u", dir2,
	  card_abstraction_.CardAbstractionName().c_str(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str(), ResolvingMethodName(method_),
	  root_bd_st_, target_st);
  sumprobs_->Read(dir, it, subtree->Root(), action_sequence, kMaxUInt);
}

#if 0
// Make sure to delete only files for the given board
void EGCFR::DeleteOldFiles(unsigned int gbd) {
  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  base_cfr_config_.CFRConfigName().c_str());
  vector<string> listing;
  GetDirectoryListing(dir, &listing);
  unsigned int num_listing = listing.size();
  unsigned int num_deleted = 0;
  for (unsigned int i = 0; i < num_listing; ++i) {
    string full_path = listing[i];
    unsigned int full_path_len = full_path.size();
    int j = full_path_len - 1;
    while (j > 0 && full_path[j] != '/') --j;
    if (strncmp(full_path.c_str() + j + 1, "compressed", 10) == 0) {
      string filename(full_path, j + 1, full_path_len - (j + 1));
      vector<string> comps;
      Split(filename.c_str(), '.', false, &comps);
      if (comps.size() != 6) {
	fprintf(stderr, "File \"%s\" has wrong number of components\n",
		full_path.c_str());
	exit(-1);
      }
      RemoveFile(full_path.c_str());
      ++num_deleted;
    }
  }
  fprintf(stderr, "%u files deleted\n", num_deleted);
}
#endif

double *EGCFR::BRGo(BettingTree *subtree, unsigned int p,
		    double **reach_probs, HandTree *hand_tree) {
  p_ = p;
  hand_tree_ = hand_tree;
  unsigned int max_card = Game::MaxCard();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(root_bd_st_);
  double *opp_probs = reach_probs[p^1];
  Node *subtree_root = subtree->Root();

  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = true;
  }
  value_calculation_ = true;
  double *total_card_probs = new double[max_card + 1];
  double sum_opp_probs;
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  CommonBetResponseCalcs(subtree_root->Street(), hands, opp_probs,
			 &sum_opp_probs, total_card_probs);
  fprintf(stderr, "Need to initialize and set street buckets\n");
  exit(-1);
  unsigned int **street_buckets = nullptr;
  // If no opponent reach the subtree with non-zero probability, then all
  // vals are zero.
  double *vals;
  if (sum_opp_probs == 0) {
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
  } else {
    vals = Process(subtree_root, 0, opp_probs, sum_opp_probs, total_card_probs,
		   street_buckets, "", subtree_root->Street());
  }
  delete [] total_card_probs;
  return vals;

#if 0
  double *our_probs = reach_probs[p];
  double sum = 0;
  *our_denom = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    double opp_sum_probs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_encoding = opp_hi * (max_card + 1) + opp_lo;
      opp_sum_probs += opp_probs[opp_encoding];
    }
    unsigned int our_encoding = our_hi * (max_card + 1) + our_lo;
    double our_prob = our_probs[our_encoding];
    sum += vals[i] * our_prob;
    *our_denom += our_prob * opp_sum_probs;
  }

  delete [] vals;
  return sum;
#endif
}

#if 0
void EGCFR::BestResponse(Node *solve_root, unsigned int base_solve_nt,
			 unsigned int solve_bd, unsigned int target_st,
			 unsigned int it, double **reach_probs,
			 double *p0_sum_ev, double *p0_denom,
			 double *p1_sum_ev, double *p1_denom) {
  BettingTree *subtree = BettingTree::BuildSubtree(solve_root);
  
  // Two calls for P1 and P0?
  Read(subtree, base_solve_nt, solve_bd, target_st, it);
  *p0_sum_ev = BRGo(subtree, 0, reach_probs, p0_denom);
  *p1_sum_ev = BRGo(subtree, 1, reach_probs, p1_denom);
  delete subtree;
}
#endif
