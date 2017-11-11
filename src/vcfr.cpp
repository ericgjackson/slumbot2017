// The VCFR class implements Vanilla CFR and other functionality that is very
// similar to Vanilla CFR.  CFR+, of course, is supported, as it is just a
// minor tweak on Vanilla CFR.  Real-game best-response calculations are
// supported.  And exhaustive computations of CBRs (counterfactual best-response
// values are supported).  Sampling variants of CFR (like PCS or outcome
// sampling) are *not* supported.
//
// The game being solved can be either abstracted or unabstracted.  For an
// abstracted system, there are a couple of changes:
// 1) We compute and store the current strategy at the beginning of each
// iteration;
// 2) We make a second pass at the end of each iteration and floor the regrets.
//
// Regrets and sumprobs can be represented as either doubles or ints.

#include <math.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>

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
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "rand.h"
#include "split.h"
#include "vcfr.h"
#include "vcfr_subgame.h"

using namespace std;

// Currently allocate for all streets.  Could be smarter and allocate just
// for streets that we have buckets for.  But we don't call this code often
// and is cheap both in terms of time and memory.
unsigned int **AllocateStreetBuckets(void) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int **street_buckets = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    street_buckets[st] = new unsigned int[num_hole_card_pairs];
  }

  return street_buckets;
}

void DeleteStreetBuckets(unsigned int **street_buckets) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    delete [] street_buckets[st];
  }
  delete [] street_buckets;
}

double *AllocateOppProbs(bool initialize) {
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;
  double *opp_probs = new double[num_enc];
  if (initialize) {
    for (unsigned int i = 0; i < num_enc; ++i) opp_probs[i] = 1.0;
  }
  return opp_probs;
}

// Called at the root of the tree.  We need to initialize total_card_probs_
// and sum_opp_probs_ because an open fold is allowed.
VCFRState::VCFRState(double *opp_probs, unsigned int **street_buckets,
		     const HandTree *hand_tree) {
  opp_probs_ = opp_probs;
  street_buckets_ = street_buckets;
  action_sequence_ = "x";
  hand_tree_ = hand_tree;
  root_bd_ = 0;
  root_bd_st_ = 0;
  unsigned int max_card1 = Game::MaxCard() + 1;
  total_card_probs_ = new double[max_card1];
  const CanonicalCards *hands = hand_tree_->Hands(0, 0);
  CommonBetResponseCalcs(0, hands, opp_probs_, &sum_opp_probs_,
			 total_card_probs_);
}
  
// Called at an internal street-initial node.  We initialize total_card_probs_
// to nullptr and sum_opp_probs_ to zero because we know we will come
// across an opp-choice node before we need those members.
VCFRState::VCFRState(double *opp_probs, const HandTree *hand_tree,
		     unsigned int st, unsigned int lbd,
		     const string &action_sequence, unsigned int root_bd,
		     unsigned int root_bd_st, unsigned int **street_buckets) {
  opp_probs_ = opp_probs;
  total_card_probs_ = nullptr;
  sum_opp_probs_ = 0;
  hand_tree_ = hand_tree;
  action_sequence_ = action_sequence;
  root_bd_ = root_bd;
  root_bd_st_ = root_bd_st;
  street_buckets_ = street_buckets;
}

// Called at an internal street-initial node.  Unlike the above constructor,
// we take a total_card_probs parameter and initialize it.
VCFRState::VCFRState(double *opp_probs, double *total_card_probs,
		     const HandTree *hand_tree, unsigned int st,
		     unsigned int lbd, const string &action_sequence,
		     unsigned int root_bd, unsigned int root_bd_st,
		     unsigned int **street_buckets) {
  opp_probs_ = opp_probs;
  total_card_probs_ = total_card_probs;
  hand_tree_ = hand_tree;
  action_sequence_ = action_sequence;
  root_bd_ = root_bd;
  root_bd_st_ = root_bd_st;
  street_buckets_ = street_buckets;
  const CanonicalCards *hands = hand_tree_->Hands(st, lbd);
  CommonBetResponseCalcs(root_bd_st_, hands, opp_probs_, &sum_opp_probs_,
			 total_card_probs_);
}

// Create a new VCFRState corresponding to taking an action of ours.
VCFRState::VCFRState(const VCFRState &pred, Node *node, unsigned int s) {
  opp_probs_ = pred.OppProbs();
  hand_tree_ = pred.GetHandTree();
  action_sequence_ = pred.ActionSequence() + node->ActionName(s);
  root_bd_ = pred.RootBd();
  root_bd_st_ = pred.RootBdSt();
  street_buckets_ = pred.StreetBuckets();
  total_card_probs_ = pred.TotalCardProbs();
  sum_opp_probs_ = pred.SumOppProbs();
}

// Create a new VCFRState corresponding to taking an opponent action.
VCFRState::VCFRState(const VCFRState &pred, Node *node, unsigned int s,
		     double *opp_probs, double sum_opp_probs,
		     double *total_card_probs) {
  opp_probs_ = opp_probs;
  hand_tree_ = pred.GetHandTree();
  action_sequence_ = pred.ActionSequence() + node->ActionName(s);
  root_bd_ = pred.RootBd();
  root_bd_st_ = pred.RootBdSt();
  street_buckets_ = pred.StreetBuckets();
  sum_opp_probs_ = sum_opp_probs;
  total_card_probs_ = total_card_probs;
}

#if 0
// If I want to update total_card_probs_ and sum_opp_probs_, do this.
void VCFRState::SetOppProbs(double *opp_probs, unsigned int st,
			    unsigned int lbd) {
  opp_probs_ = opp_probs;
  const CanonicalCards *hands = hand_tree_->Hands(st, lbd);
  CommonBetResponseCalcs(root_bd_st_, hands, opp_probs_, &sum_opp_probs_,
			 total_card_probs_);
}
#endif

// We don't own any of the arrays.  Caller must delete.
VCFRState::~VCFRState(void) {
}

// Unabstracted, integer regrets
void VCFR::UpdateRegrets(Node *node, double *vals, double **succ_vals,
			 int *regrets) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  
  int floor = regret_floors_[st];
  int ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      int *my_regrets = regrets + i * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	// Need different implementation for doubles
	int di = lrint(d * regret_scaling_[st]);
	int ri = my_regrets[s] + di;
	if (ri < floor) {
	  my_regrets[s] = floor;
	} else if (ri > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = ri;
	}
      }
    }
  } else {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      int *my_regrets = regrets + i * num_succs;
      bool overflow = false;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	my_regrets[s] += lrint(d * regret_scaling_[st]);
	if (my_regrets[s] < -2000000000 || my_regrets[s] > 2000000000) {
	  overflow = true;
	}
      }
      if (overflow) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  my_regrets[s] /= 2;
	}
      }
    }
  }
}

// Abstracted, integer regrets
// No flooring here.  Will be done later.
void VCFR::UpdateRegretsBucketed(Node *node, unsigned int **street_buckets,
				 double *vals, double **succ_vals,
				 int *regrets) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);

  int ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      int *my_regrets = regrets + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	// Need different implementation for doubles
	int di = lrint(d * regret_scaling_[st]);
	int ri = my_regrets[s] + di;
	if (ri > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = ri;
	}
      }
    }
  } else {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      int *my_regrets = regrets + b * num_succs;
      bool overflow = false;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double d = succ_vals[s][i] - vals[i];
	my_regrets[s] += lrint(d * regret_scaling_[st]);
	if (my_regrets[s] < -2000000000 || my_regrets[s] > 2000000000) {
	  overflow = true;
	}
      }
      if (overflow) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  my_regrets[s] /= 2;
	}
      }
    }
  }
}

// Unabstracted, double regrets
// This implementation does not round regrets to ints, nor do scaling.
void VCFR::UpdateRegrets(Node *node, double *vals, double **succ_vals,
			 double *regrets) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  
  double floor = regret_floors_[st];
  double ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      double *my_regrets = regrets + i * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double newr = my_regrets[s] + succ_vals[s][i] - vals[i];
	if (newr < floor) {
	  my_regrets[s] = floor;
	} else if (newr > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = newr;
	}
      }
    }
  } else {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      double *my_regrets = regrets + i * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	my_regrets[s] += succ_vals[s][i] - vals[i];
      }
    }
  }
}

// Abstracted, double regrets
// This implementation does not round regrets to ints, nor do scaling.
// No flooring here.  Will be done later.
void VCFR::UpdateRegretsBucketed(Node *node, unsigned int **street_buckets,
				 double *vals, double **succ_vals,
				 double *regrets) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  
  double ceiling = regret_ceilings_[st];
  if (nn_regrets_) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      double *my_regrets = regrets + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double newr = my_regrets[s] + succ_vals[s][i] - vals[i];
	if (newr > ceiling) {
	  my_regrets[s] = ceiling;
	} else {
	  my_regrets[s] = newr;
	}
      }
    }
  } else {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      double *my_regrets = regrets + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	my_regrets[s] += succ_vals[s][i] - vals[i];
      }
    }
  }
}

double *VCFR::OurChoice(Node *node, unsigned int lbd, const VCFRState &state) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int nt = node->NonterminalID();
  double *vals = nullptr;
  double **succ_vals = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    VCFRState succ_state(state, node, s);
    succ_vals[s] = Process(node->IthSucc(s), lbd, succ_state, st);
  }
  if (num_succs == 1) {
    vals = succ_vals[0];
    succ_vals[0] = nullptr;
  } else {
    unsigned int **street_buckets = state.StreetBuckets();
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
    if (best_response_streets_[st]) {
      if (always_call_preflop_ && st == 0) {
	unsigned int csi = node->CallSuccIndex();
	for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	  vals[i] = succ_vals[csi][i];
	}
      } else {
	unique_ptr<unsigned int []> succ_counts(new unsigned int [num_succs]);
	for (unsigned int s = 0; s < num_succs; ++s) succ_counts[s] = 0;
	for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	  double max_val = succ_vals[0][i];
	  unsigned int best_s = 0;
	  for (unsigned int s = 1; s < num_succs; ++s) {
	    double sv = succ_vals[s][i];
	    if (sv > max_val) {max_val = sv; best_s = s;}
	  }
	  vals[i] = max_val;
	  ++succ_counts[best_s];
	}
      }
    } else {
      bool bucketed = ! buckets_.None(st) &&
	node->LastBetTo() < card_abstraction_.BucketThreshold(st);
      if (bucketed && ! value_calculation_) {
	// When running CFR+ on a bucketed system, we want to use the
	// regrets from the beginning of the iteration to determine the
	// current strategy, even as we update the regrets.  So we store
	// the current strategy in current_strategy_.
	double *d_all_current_probs;
	current_strategy_->Values(p_, st, nt, &d_all_current_probs);
	for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	  unsigned int b = street_buckets[st][i];
	  double *my_current_probs = d_all_current_probs + b * num_succs;
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    vals[i] += succ_vals[s][i] * my_current_probs[s];
	  }
	}
	if (! value_calculation_ && ! pre_phase_) {
	  if (regrets_->Ints(p_, st)) {
	    int *i_all_regrets;
	    regrets_->Values(p_, st, nt, &i_all_regrets);
	    UpdateRegretsBucketed(node, street_buckets, vals, succ_vals,
				  i_all_regrets);
	  } else {
	    double *d_all_regrets;
	    regrets_->Values(p_, st, nt, &d_all_regrets);
	    UpdateRegretsBucketed(node, street_buckets, vals, succ_vals,
				  d_all_regrets);
	  }
	}
      } else {
	// If we are doing a value calculation then we are not updating
	// regrets so we do not need this separate copy of the current
	// strategy as we do in CFR+ (see above).
	double *current_probs = new double[num_succs];
	unsigned int default_succ_index = node->DefaultSuccIndex();
	double *d_all_cs_vals = nullptr;
	int *i_all_cs_vals = nullptr;
	bool nonneg;
	double explore;
	if (value_calculation_) {
	  // For example, when called from build_cbrs.
	  if (sumprobs_->Ints(p_, st)) {
	    sumprobs_->Values(p_, st, nt, &i_all_cs_vals);
	  } else {
	    sumprobs_->Values(p_, st, nt, &d_all_cs_vals);
	  }
	  nonneg = true;
	  // Don't want to impose exploration when working off of sumprobs.
	  explore = 0;
	} else {
	  if (regrets_->Ints(p_, st)) {
	    regrets_->Values(p_, st, nt, &i_all_cs_vals);
	  } else {
	    regrets_->Values(p_, st, nt, &d_all_cs_vals);
	  }
	  nonneg = nn_regrets_ && regret_floors_[st] >= 0;
	  explore = explore_;
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
	if (bucketed) {
	  if (i_all_cs_vals) {
	    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	      unsigned int b = street_buckets[st][i];
	      int *my_cs_vals = i_all_cs_vals + b * num_succs;
	      RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform_,
			     default_succ_index, explore,
			     num_nonterminal_succs, nonterminal_succs,
			     current_probs);
	      for (unsigned int s = 0; s < num_succs; ++s) {
		vals[i] += succ_vals[s][i] * current_probs[s];
	      }
	    }
	  } else {
	    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	      unsigned int b = street_buckets[st][i];
	      double *my_cs_vals = d_all_cs_vals + b * num_succs;
	      RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform_,
			     default_succ_index, explore,
			     num_nonterminal_succs, nonterminal_succs,
			     current_probs);
	      for (unsigned int s = 0; s < num_succs; ++s) {
		vals[i] += succ_vals[s][i] * current_probs[s];
	      }
	    }
	  }
	} else {
	  if (i_all_cs_vals) {
	    int *i_bd_cs_vals =
	      i_all_cs_vals + lbd * num_hole_card_pairs * num_succs;
	    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	      int *my_cs_vals = i_bd_cs_vals + i * num_succs;
	      RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform_,
			     default_succ_index, explore,
			     num_nonterminal_succs, nonterminal_succs,
			     current_probs);
	      for (unsigned int s = 0; s < num_succs; ++s) {
		vals[i] += succ_vals[s][i] * current_probs[s];
	      }
	    }
	    if (! value_calculation_ && ! pre_phase_) {
	      UpdateRegrets(node, vals, succ_vals, i_bd_cs_vals);
	    }
	  } else {
	    double *d_bd_cs_vals =
	      d_all_cs_vals + lbd * num_hole_card_pairs * num_succs;
	    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	      double *my_cs_vals = d_bd_cs_vals + i * num_succs;
	      RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform_,
			     default_succ_index, explore, num_nonterminal_succs,
			     nonterminal_succs, current_probs);
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      vals[i] += succ_vals[s][i] * current_probs[s];
	    }
	  }
	    if (! value_calculation_ && ! pre_phase_) {
	      UpdateRegrets(node, vals, succ_vals, d_bd_cs_vals);
	    }
	  }
	}
	delete [] current_probs;
	delete [] nonterminal_succs;
      }
    }
  }

  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_vals[s];
  }
  delete [] succ_vals;

  return vals;
}

double *VCFR::OppChoice(Node *node, unsigned int lbd, const VCFRState &state) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  const HandTree *hand_tree = state.GetHandTree();
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;

  double *opp_probs = state.OppProbs();
  double **succ_opp_probs = new double *[num_succs];
  if (num_succs == 1) {
    succ_opp_probs[0] = new double[num_enc];
    for (unsigned int i = 0; i < num_enc; ++i) {
      succ_opp_probs[0][i] = opp_probs[i];
    }
  } else {
    unsigned int **street_buckets = state.StreetBuckets();
    unsigned int nt = node->NonterminalID();
    unsigned int opp = p_^1;
    for (unsigned int s = 0; s < num_succs; ++s) {
      succ_opp_probs[s] = new double[num_enc];
      for (unsigned int i = 0; i < num_enc; ++i) succ_opp_probs[s][i] = 0;
    }

    // The "all" values point to the values for all hands.
    double *d_all_current_probs = nullptr;
    double *d_all_cs_vals = nullptr;
    int *i_all_cs_vals = nullptr;

    double explore;
    if (value_calculation_ && ! br_current_) explore = 0;
    else                                     explore = explore_;

    bool bucketed = ! buckets_.None(st) &&
      node->LastBetTo() < card_abstraction_.BucketThreshold(st);

    if (bucketed && ! value_calculation_) {
      current_strategy_->Values(opp, st, nt, &d_all_current_probs);
    } else {
      // cs_vals are the current strategy values; i.e., the values we pass into
      // RegretsToProbs() in order to get the current strategy.  In VCFR, these
      // are regrets; in a best-response calculation, they are (normally)
      // sumprobs.

      if (value_calculation_ && ! br_current_) {
	if (sumprobs_->Ints(opp, st)) {
	  sumprobs_->Values(opp, st, nt, &i_all_cs_vals);
	} else {
	  sumprobs_->Values(opp, st, nt, &d_all_cs_vals);
	}
      } else {
	if (regrets_->Ints(opp, st)) {
	  regrets_->Values(opp, st, nt, &i_all_cs_vals);
	} else {
	  regrets_->Values(opp, st, nt, &d_all_cs_vals);
	}
      }
    }

    // The "all" values point to the values for all hands.
    double *d_all_sumprobs = nullptr;
    int *i_all_sumprobs = nullptr;
    // sumprobs_->Players(opp) check is there because in asymmetric systems
    // (e.g., endgame solving with CFR-D method) we are only saving probs for
    // one player.
    // Don't update sumprobs during pre phase
    if (! pre_phase_ && ! value_calculation_ && sumprob_streets_[st] &&
	sumprobs_->Players(opp)) {
      if (sumprobs_->Ints(opp, st)) {
	sumprobs_->Values(opp, st, nt, &i_all_sumprobs);
      } else {
	sumprobs_->Values(opp, st, nt, &d_all_sumprobs);
      }
    }

    // These values will point to the values for the current board
    double *d_cs_vals = nullptr, *d_sumprobs = nullptr;
    int *i_cs_vals = nullptr, *i_sumprobs = nullptr;

    if (bucketed) {
      i_cs_vals = i_all_cs_vals;
      d_cs_vals = d_all_cs_vals;
      i_sumprobs = i_all_sumprobs;
      d_sumprobs = d_all_sumprobs;
    } else {
      if (i_all_cs_vals) {
	i_cs_vals = i_all_cs_vals + lbd * num_hole_card_pairs * num_succs;
      } else {
	d_cs_vals = d_all_cs_vals + lbd * num_hole_card_pairs * num_succs;
      }
      if (i_all_sumprobs) {
	i_sumprobs = i_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
      }
      if (d_all_sumprobs) {
	d_sumprobs = d_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
      }
    }

    bool nonneg;
    if (value_calculation_ && ! br_current_) {
      nonneg = true;
    } else {
      nonneg = nn_regrets_ && regret_floors_[st] >= 0;
    }
    // No sumprob update if a) doing value calculation (e.g., RGBR), b)
    // we have no sumprobs (e.g., in asymmetric CFR), or c) we are during
    // the hard warmup period
    if (bucketed && ! value_calculation_) {
      if (d_sumprobs) {
	// Double sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || d_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbsBucketed(node, street_buckets, hands, nonneg, it_,
				soft_warmup_, hard_warmup_, update_sumprobs,
				opp_probs, succ_opp_probs,
				d_all_current_probs, d_sumprobs);
      } else {
	// Int sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || i_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbsBucketed(node, street_buckets, hands, nonneg, it_,
				soft_warmup_, hard_warmup_, update_sumprobs,
				sumprob_scaling_, opp_probs, succ_opp_probs,
				d_all_current_probs, i_sumprobs);
      }
    } else {
      if (i_cs_vals) {
	if (d_sumprobs) {
	  // Int regrets, double sumprobs
	  bool update_sumprobs =
	    ! (value_calculation_ || d_sumprobs == nullptr ||
	       (hard_warmup_ > 0 && it_ <= hard_warmup_));
	  ProcessOppProbs(node, hands, bucketed, street_buckets, nonneg,
			  uniform_, explore, it_, soft_warmup_, hard_warmup_,
			  update_sumprobs, opp_probs, succ_opp_probs,
			  i_cs_vals, d_sumprobs);
	} else {
	  // Int regrets and sumprobs
	  bool update_sumprobs =
	    ! (value_calculation_ || i_sumprobs == nullptr ||
	       (hard_warmup_ > 0 && it_ <= hard_warmup_));
	  ProcessOppProbs(node, hands, bucketed, street_buckets, nonneg,
			  uniform_, explore, it_, soft_warmup_, hard_warmup_,
			  update_sumprobs, sumprob_scaling_, opp_probs,
			  succ_opp_probs, i_cs_vals, i_sumprobs);
	}
      } else {
	// Double regrets and sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || d_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbs(node, hands, bucketed, street_buckets, nonneg,
			uniform_, explore, it_, soft_warmup_, hard_warmup_,
			update_sumprobs, opp_probs, succ_opp_probs,
			d_cs_vals, d_sumprobs);
      }
    }
  }

  double *vals = nullptr;
  double succ_sum_opp_probs;
  for (unsigned int s = 0; s < num_succs; ++s) {
    double *succ_total_card_probs = new double[max_card1];
    CommonBetResponseCalcs(st, hands, succ_opp_probs[s], &succ_sum_opp_probs,
			   succ_total_card_probs);
    if (prune_ && succ_sum_opp_probs == 0) {
      delete [] succ_total_card_probs;
      continue;
    }
    VCFRState succ_state(state, node, s, succ_opp_probs[s], succ_sum_opp_probs,
			 succ_total_card_probs);
    double *succ_vals = Process(node->IthSucc(s), lbd, succ_state, st);
    if (vals == nullptr) {
      vals = succ_vals;
    } else {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	vals[i] += succ_vals[i];
      }
      delete [] succ_vals;
    }
    delete [] succ_total_card_probs;
  }
  if (vals == nullptr) {
    // This can happen if there were non-zero opp probs on the prior street,
    // but the board cards just dealt blocked all the opponent hands with
    // non-zero probability.
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_opp_probs[s];
  }
  delete [] succ_opp_probs;

  return vals;
}

class VCFRThread {
public:
  VCFRThread(VCFR *vcfr, unsigned int thread_index, unsigned int num_threads,
	     Node *node, const string &action_sequence, double *opp_probs,
	     const HandTree *hand_tree, unsigned int *prev_canons);
  ~VCFRThread(void);
  void Run(void);
  void Join(void);
  void Go(void);
  double *RetVals(void) const {return ret_vals_;}
private:
  VCFR *vcfr_;
  unsigned int thread_index_;
  unsigned int num_threads_;
  Node *node_;
  const string &action_sequence_;
  double *opp_probs_;
  const HandTree *hand_tree_;
  unsigned int *prev_canons_;
  double *ret_vals_;
  pthread_t pthread_id_;
};

VCFRThread::VCFRThread(VCFR *vcfr, unsigned int thread_index,
		       unsigned int num_threads, Node *node,
		       const string &action_sequence, double *opp_probs,
		       const HandTree *hand_tree, unsigned int *prev_canons) :
  action_sequence_(action_sequence) {
  vcfr_ = vcfr;
  thread_index_ = thread_index;
  num_threads_ = num_threads;
  node_ = node;
  opp_probs_ = opp_probs;
  hand_tree_ = hand_tree;
  prev_canons_ = prev_canons;
}

VCFRThread::~VCFRThread(void) {
  delete [] ret_vals_;
}

static void *vcfr_thread_run(void *v_t) {
  VCFRThread *t = (VCFRThread *)v_t;
  t->Go();
  return NULL;
}

void VCFRThread::Run(void) {
  pthread_create(&pthread_id_, NULL, vcfr_thread_run, this);
}

void VCFRThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

void VCFRThread::Go(void) {
  unsigned int st = node_->Street();
  unsigned int pst = node_->Street() - 1;
  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int num_prev_hole_card_pairs = Game::NumHoleCardPairs(pst);
  Card max_card1 = Game::MaxCard() + 1;
  ret_vals_ = new double[num_prev_hole_card_pairs];
  for (unsigned int i = 0; i < num_prev_hole_card_pairs; ++i) ret_vals_[i] = 0;
  for (unsigned int bd = thread_index_; bd < num_boards; bd += num_threads_) {
    unsigned int **street_buckets = AllocateStreetBuckets();
    VCFRState state(opp_probs_, hand_tree_, st, bd, action_sequence_, 0, 0,
		    street_buckets);
    // Initialize buckets for this street
    vcfr_->SetStreetBuckets(st, bd, state);
    double *bd_vals = vcfr_->Process(node_, bd, state, st);
    const CanonicalCards *hands = hand_tree_->Hands(st, bd);
    unsigned int board_variants = BoardTree::NumVariants(st, bd);
    unsigned int num_hands = hands->NumRaw();
    for (unsigned int h = 0; h < num_hands; ++h) {
      const Card *cards = hands->Cards(h);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      unsigned int prev_canon = prev_canons_[enc];
      ret_vals_[prev_canon] += board_variants * bd_vals[h];
    }
    delete [] bd_vals;
    DeleteStreetBuckets(street_buckets);
  }
}

// Divide work at a street-initial node between multiple threads.  Spawns
// the threads, joins them, aggregates the resulting CVs.
// Only support splitting on the flop for now.
// Ugly that we pass prev_canons in.
void VCFR::Split(Node *node, double *opp_probs, const HandTree *hand_tree,
		 const string &action_sequence, unsigned int *prev_canons,
		 double *vals) {
  unsigned int nst = node->Street();
  unsigned int pst = nst - 1;
  unsigned int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
  unique_ptr<VCFRThread * []> threads(new VCFRThread *[num_threads_]);
  for (unsigned int t = 0; t < num_threads_; ++t) {
    threads[t] = new VCFRThread(this, t, num_threads_, node, action_sequence,
				opp_probs, hand_tree, prev_canons);
  }
  for (unsigned int t = 1; t < num_threads_; ++t) {
    threads[t]->Run();
  }
  // Do first thread in main thread
  threads[0]->Go();
  for (unsigned int t = 1; t < num_threads_; ++t) {
    threads[t]->Join();
  }
  for (unsigned int t = 0; t < num_threads_; ++t) {
    double *t_vals = threads[t]->RetVals();
    for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) {
      vals[i] += t_vals[i];
    }
    delete threads[t];
  }
}

void VCFR::SetStreetBuckets(unsigned int st, unsigned int gbd,
			    const VCFRState &state) {
  if (buckets_.None(st)) return;
  unsigned int num_board_cards = Game::NumBoardCards(st);
  const Card *board = BoardTree::Board(st, gbd);
  Card cards[7];
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    cards[i + 2] = board[i];
  }
  unsigned int lbd = BoardTree::LocalIndex(state.RootBdSt(), state.RootBd(),
					   st, gbd);

  const HandTree *hand_tree = state.GetHandTree();
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int **street_buckets = state.StreetBuckets();
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    unsigned int h;
    if (st == max_street) {
      // Hands on final street were reordered by hand strength, but
      // bucket lookup requires the unordered hole card pair index
      const Card *hole_cards = hands->Cards(i);
      cards[0] = hole_cards[0];
      cards[1] = hole_cards[1];
      unsigned int hcp = HCPIndex(st, cards);
      h = gbd * num_hole_card_pairs + hcp;
    } else {
      h = gbd * num_hole_card_pairs + i;
    }
    street_buckets[st][i] = buckets_.Bucket(st, h);
  }
}

double *VCFR::StreetInitial(Node *node, unsigned int plbd,
			    const VCFRState &state) {
  unsigned int nst = node->Street();
  unsigned int pst = nst - 1;
  unsigned int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  if (nst == subgame_street_ && ! subgame_) {
    if (pre_phase_) {
      SpawnSubgame(node, plbd, state.ActionSequence(), state.OppProbs());
      // Code expects values to be returned so we return all zeroes
      double *vals = new double[prev_num_hole_card_pairs];
      for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
      return vals;
    } else {
      unsigned int p = node->PlayerActing();
      unsigned int nt = node->NonterminalID();
      double *final_vals = final_vals_[p][nt][plbd];
      if (final_vals == nullptr) {
	fprintf(stderr, "No final vals for %u %u %u?!?\n", p, nt, plbd);
	exit(-1);
      }
      final_vals_[p][nt][plbd] = nullptr;
      return final_vals;
    }
  }
  const HandTree *hand_tree = state.GetHandTree();
  const CanonicalCards *pred_hands = hand_tree->Hands(pst, plbd);
  Card max_card = Game::MaxCard();
  unsigned int num_encodings = (max_card + 1) * (max_card + 1);
  unsigned int *prev_canons = new unsigned int[num_encodings];
  double *vals = new double[prev_num_hole_card_pairs];
  for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) > 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_encoding = prev_cards[0] * (max_card + 1) +
	prev_cards[1];
      prev_canons[prev_encoding] = ph;
    }
  }
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_encoding = prev_cards[0] * (max_card + 1) +
	prev_cards[1];
      unsigned int pc = prev_canons[pred_hands->Canon(ph)];
      prev_canons[prev_encoding] = pc;
    }
  }

  if (nst == 1 && subgame_street_ == kMaxUInt && num_threads_ > 1) {
    // Currently only flop supported
    Split(node, state.OppProbs(), state.GetHandTree(), state.ActionSequence(),
	  prev_canons, vals);
  } else {
    unsigned int pgbd = BoardTree::GlobalIndex(state.RootBdSt(),
					       state.RootBd(), pst, plbd);
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
    for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      unsigned int nlbd = BoardTree::LocalIndex(state.RootBdSt(),
						state.RootBd(), nst, ngbd);

      const CanonicalCards *hands = hand_tree->Hands(nst, nlbd);
    
      SetStreetBuckets(nst, ngbd, state);
      // I can pass unset values for sum_opp_probs and total_card_probs.  I
      // know I will come across an opp choice node before getting to a terminal
      // node.
      double *next_vals = Process(node, nlbd, state, nst);

      unsigned int board_variants = BoardTree::NumVariants(nst, ngbd);
      unsigned int num_next_hands = hands->NumRaw();
      for (unsigned int nh = 0; nh < num_next_hands; ++nh) {
	const Card *cards = hands->Cards(nh);
	Card hi = cards[0];
	Card lo = cards[1];
	unsigned int encoding = hi * (max_card + 1) + lo;
	unsigned int prev_canon = prev_canons[encoding];
	vals[prev_canon] += board_variants * next_vals[nh];
      }
      delete [] next_vals;
    }
  }
  // Scale down the values of the previous-street canonical hands
  double scale_down = Game::StreetPermutations(nst);
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    unsigned int prev_hand_variants = pred_hands->NumVariants(ph);
    if (prev_hand_variants > 0) {
      // Is this doing the right thing?
      vals[ph] /= scale_down * prev_hand_variants;
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

void VCFR::Post(unsigned int t) {
  // Set subgame_running_[t] to false *before* sem_post.  If we didn't do that,
  // we might break out of sem_wait in main thread and find no threads with
  // available status.
  // The following is possible.  Threads 1 and 2 finish at near the same time.
  // Thread 2 posts, thread 1 sets subgame_running[1] to false, the manager
  // chooses thread 1 to work on next.  That's fine, I think.
  subgame_running_[t] = false;
  int ret = sem_post(&available_);
  if (ret != 0) {
    fprintf(stderr, "sem_post failed\n");
    exit(-1);
  }
}

static unsigned int g_num_active = 0;

// This breaks if we get Post() before call to WaitForFinalSubgames(), but
// not corresponding join.  Saw num_remaining two but num active three.
void VCFR::WaitForFinalSubgames(void) {
  unsigned int num_remaining = 0;
  for (unsigned int t = 0; t < num_threads_; ++t) {
    // This was buggy
    // if (subgame_running_[t]) ++num_remaining;
    if (active_subgames_[t]) ++num_remaining;
  }
  if (num_remaining != g_num_active) {
    fprintf(stderr, "Expect num_remaining %u to match num_active %u\n",
	    num_remaining, g_num_active);
    exit(-1);
  }
  while (num_remaining > 0) {
    while (sem_wait(&available_) == EINTR) ;
    unsigned int t;
    for (t = 0; t < num_threads_; ++t) {
      if (! subgame_running_[t] && active_subgames_[t]) {
	VCFRSubgame *subgame = active_subgames_[t];
	if (subgame == nullptr) {
	  fprintf(stderr, "Subgame finished, but no subgame object?!?\n");
	  exit(-1);
	}
	pthread_join(pthread_ids_[t], NULL);
	Node *root = subgame->Root();
	unsigned int p = root->PlayerActing();
	unsigned int nt = root->NonterminalID();
	unsigned int root_bd = subgame->RootBd();
	final_vals_[p][nt][root_bd] = subgame->FinalVals();
	delete subgame;
	active_subgames_[t] = nullptr;
	--num_remaining;
	--g_num_active;
	break;
      }
    }
    // It's possible for sem_wait() to return even though there is no thread
    // ready to be joined.
  }
  if (g_num_active > 0) {
    fprintf(stderr, "Num active %u at end of WaitForFinalSubgames()\n",
	    g_num_active);
    exit(-1);
  }
}

static void *thread_run(void *v_sg) {
  VCFRSubgame *sg = (VCFRSubgame *)v_sg;
  sg->Go();
  return NULL;
}

void VCFR::SpawnSubgame(Node *node, unsigned int bd,
			const string &action_sequence, double *opp_probs) {
  VCFRSubgame *subgame =
    new VCFRSubgame(card_abstraction_, betting_abstraction_, cfr_config_,
		    buckets_, node, bd, action_sequence, this);
  subgame->SetBestResponseStreets(best_response_streets_);
  subgame->SetBRCurrent(br_current_);
  subgame->SetValueCalculation(value_calculation_);
  // Wait for thread to be available
  while (sem_wait(&available_) == EINTR) ;

  // Find a thread that is not working
  unsigned int t;
  for (t = 0; t < num_threads_; ++t) {
    if (! subgame_running_[t]) break;
  }
  if (t == num_threads_) {
    fprintf(stderr, "sem_wait returned but no thread available\n");
    exit(-1);
  }
  VCFRSubgame *old_subgame = active_subgames_[t];
  if (old_subgame) {
    pthread_join(pthread_ids_[t], NULL);
    Node *root = old_subgame->Root();
    unsigned int p = root->PlayerActing();
    unsigned int nt = root->NonterminalID();
    unsigned int root_bd = old_subgame->RootBd();
    final_vals_[p][nt][root_bd] = old_subgame->FinalVals();
    delete old_subgame;
    active_subgames_[t] = nullptr;
    --g_num_active;
    if (num_threads_ == 1 && g_num_active != 0) {
      fprintf(stderr, "num_active %i\n", g_num_active);
      exit(-1);
    }
  }

  // Launch the current subgame
  subgame_running_[t] = true;
  active_subgames_[t] = subgame;
  // I could pass these into the constructor, no?
  subgame->SetP(p_);
  subgame->SetTargetP(target_p_);
  subgame->SetIt(it_);
  subgame->SetOppProbs(opp_probs);
  subgame->SetThreadIndex(t);
  subgame->SetLastCheckpointIt(last_checkpoint_it_);
  ++g_num_active;
  if (num_threads_ == 1 && g_num_active != 1) {
    fprintf(stderr, "num_active %i\n", g_num_active);
    exit(-1);
  }
  pthread_create(&pthread_ids_[t], NULL, thread_run, subgame);
}

double *VCFR::Process(Node *node, unsigned int lbd, const VCFRState &state,
		      unsigned int last_st) {
  unsigned int st = node->Street();
  if (node->Terminal()) {
    if (node->NumRemaining() == 1) {
      return Fold(node, p_, state.GetHandTree()->Hands(st, lbd),
		  state.OppProbs(), state.SumOppProbs(),
		  state.TotalCardProbs());
    } else {
      return Showdown(node, state.GetHandTree()->Hands(st, lbd),
		      state.OppProbs(), state.SumOppProbs(),
		      state.TotalCardProbs());
    }
  }
  if (st > last_st) {
    return StreetInitial(node, lbd, state);
  }
  if (node->PlayerActing() == p_) {
    return OurChoice(node, lbd, state);
  } else {
    return OppChoice(node, lbd, state);
  }
}

void VCFR::SetCurrentStrategy(Node *node) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int default_succ_index = node->DefaultSuccIndex();
  unsigned int p = node->PlayerActing();

  if (current_strategy_->Players(p) && ! buckets_.None(st) &&
      node->LastBetTo() < card_abstraction_.BucketThreshold(st) &&
      num_succs > 1) {
    // In RGBR calculation, for example, only want to set for opp
    
    unsigned int num_buckets = buckets_.NumBuckets(st);
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

    double *d_all_current_strategy;
    current_strategy_->Values(p, st, nt, &d_all_current_strategy);
    double *d_all_cs_vals = nullptr;
    int *i_all_cs_vals = nullptr;
    bool nonneg;
    double explore;
    if (value_calculation_ && ! br_current_) {
      // Use average strategy for the "cs vals"
      if (sumprobs_->Ints(p, st)) {
	sumprobs_->Values(p, st, nt, &i_all_cs_vals);
      } else {
	sumprobs_->Values(p, st, nt, &d_all_cs_vals);
      }
      nonneg = true;
      explore = 0;
    } else {
      // Use regrets for the "cs vals"
      if (regrets_->Ints(p, st)) {
	regrets_->Values(p, st, nt, &i_all_cs_vals);
      } else {
	regrets_->Values(p, st, nt, &d_all_cs_vals);
      }
      nonneg = nn_regrets_ && regret_floors_[st] >= 0;
      explore = explore_;
    }
    if (i_all_cs_vals) {
      for (unsigned int b = 0; b < num_buckets; ++b) {
	int *cs_vals = i_all_cs_vals + b * num_succs;
	double *probs = d_all_current_strategy + b * num_succs;
	RegretsToProbs(cs_vals, num_succs, nonneg, uniform_, default_succ_index,
		       explore, num_nonterminal_succs, nonterminal_succs,
		       probs);
      }
    } else {
      for (unsigned int b = 0; b < num_buckets; ++b) {
	double *cs_vals = d_all_cs_vals + b * num_succs;
	double *probs = d_all_current_strategy + b * num_succs;
	RegretsToProbs(cs_vals, num_succs, nonneg, uniform_, default_succ_index,
		       explore, num_nonterminal_succs, nonterminal_succs,
		       probs);
      }
    }
    delete [] nonterminal_succs;
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    SetCurrentStrategy(node->IthSucc(s));
  }
}

void VCFR::SetBestResponseStreets(bool *sts) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = sts[st];
  }
}

VCFR::VCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	   const CFRConfig &cc, const Buckets &buckets,
	   const BettingTree *betting_tree, unsigned int num_threads) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(buckets), betting_tree_(betting_tree) {
  num_threads_ = num_threads;
  target_p_ = kMaxUInt; // Should set this somehow
  num_players_ = Game::NumPlayers();
  subgame_street_ = cfr_config_.SubgameStreet();
  nn_regrets_ = cfr_config_.NNR();
  uniform_ = cfr_config_.Uniform();
  soft_warmup_ = cfr_config_.SoftWarmup();
  hard_warmup_ = cfr_config_.HardWarmup();
  explore_ = cfr_config_.Explore();
  double_regrets_ = cfr_config_.DoubleRegrets();
  double_sumprobs_ = cfr_config_.DoubleSumprobs();
  always_call_preflop_ = false;

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

  sumprob_streets_ = new bool[max_street + 1];
  const vector<unsigned int> &ssv = cfr_config_.SumprobStreets();
  unsigned int num_ssv = ssv.size();
  if (num_ssv == 0) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      sumprob_streets_[st] = true;
    }
  } else {
    for (unsigned int st = 0; st <= max_street; ++st) {
      sumprob_streets_[st] = false;
    }
    for (unsigned int i = 0; i < num_ssv; ++i) {
      unsigned int st = ssv[i];
      sumprob_streets_[st] = true;
    }
  }

  regret_floors_ = new int[max_street + 1];
  const vector<int> &fv = cfr_config_.RegretFloors();
  if (fv.size() == 0) {
    for (unsigned int s = 0; s <= max_street; ++s) {
      regret_floors_[s] = 0;
    }
  } else {
    if (fv.size() < max_street + 1) {
      fprintf(stderr, "Regret floor vector too small\n");
      exit(-1);
    }
    for (unsigned int s = 0; s <= max_street; ++s) {
      if (fv[s] == 1) regret_floors_[s] = kMinInt;
      else            regret_floors_[s] = fv[s];
    }
  }

  regret_ceilings_ = new int[max_street + 1];
  const vector<int> &cv = cfr_config_.RegretCeilings();
  if (cv.size() == 0) {
    for (unsigned int s = 0; s <= max_street; ++s) {
      regret_ceilings_[s] = kMaxInt;
    }
  } else {
    if (cv.size() < max_street + 1) {
      fprintf(stderr, "Regret ceiling vector too small\n");
      exit(-1);
    }
    for (unsigned int s = 0; s <= max_street; ++s) {
      if (cv[s] == 0) regret_ceilings_[s] = kMaxInt;
      else            regret_ceilings_[s] = cv[s];
    }
  }

  regret_scaling_ = new double[max_street + 1];
  sumprob_scaling_ = new double[max_street + 1];
  const vector<double> &rv = cfr_config_.RegretScaling();
  if (rv.size() == 0) {
    for (unsigned int s = 0; s <= max_street; ++s) {
      regret_scaling_[s] = 1.0;
    }
  } else {
    if (rv.size() < max_street + 1) {
      fprintf(stderr, "Regret scaling vector too small\n");
      exit(-1);
    }
    for (unsigned int s = 0; s <= max_street; ++s) {
      regret_scaling_[s] = rv[s];
    }
  }
  const vector<double> &sv = cfr_config_.SumprobScaling();
  if (sv.size() == 0) {
    for (unsigned int s = 0; s <= max_street; ++s) {
      sumprob_scaling_[s] = 1.0;
    }
  } else {
    if (sv.size() < max_street + 1) {
      fprintf(stderr, "Sumprob scaling vector too small\n");
      exit(-1);
    }
    for (unsigned int s = 0; s <= max_street; ++s) {
      sumprob_scaling_[s] = sv[s];
    }
  }

  subgame_running_ = new bool[num_threads_];
  active_subgames_ = new VCFRSubgame *[num_threads_];
  pthread_ids_ = new pthread_t[num_threads_];
  for (unsigned int t = 0; t < num_threads_; ++t) {
    subgame_running_[t] = false;
    active_subgames_[t] = nullptr;
  }
  int ret = sem_init(&available_, 0, 0);
  if (ret != 0) {
    fprintf(stderr, "sem_init failed\n");
    exit(-1);
  }

  if (betting_tree_ == nullptr || subgame_street_ > max_street) {
    final_vals_ = nullptr;
  } else {
    // Should only do this if we are not a subgame
    unsigned int num_subgame_boards = BoardTree::NumBoards(subgame_street_ - 1);
    final_vals_ = new double ***[2];
    for (unsigned int p = 0; p <= 1; ++p) {
      unsigned int num_nt = betting_tree_->NumNonterminals(p, subgame_street_);
      final_vals_[p] = new double **[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	final_vals_[p][i] = new double *[num_subgame_boards];
	for (unsigned int bd = 0; bd < num_subgame_boards; ++bd) {
	  final_vals_[p][i][bd] = nullptr;
	}
      }
    }
  }

  it_ = 0;
  last_checkpoint_it_ = 0;
  // Defaults
  subgame_ = false;
  best_response_streets_ = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = false;
  }
  br_current_ = false;
  value_calculation_ = false;
  // Whether we prune branches if no opponent hand reaches.  Normally true,
  // but false when calculating CBRs.
  prune_ = true;
  // This should be coordinated with how we determine whether to update
  // sumprobs in OppChoice().
  pre_phase_ = false;
}

VCFR::~VCFR(void) {
  sem_destroy(&available_);
  delete [] subgame_running_;
  delete [] active_subgames_;
  delete [] pthread_ids_;

  if (final_vals_) {
    unsigned int num_subgame_boards = BoardTree::NumBoards(subgame_street_ - 1);
    for (unsigned int p1 = 0; p1 <= 1; ++p1) {
      unsigned int num_nt = betting_tree_->NumNonterminals(p1, subgame_street_);
      for (unsigned int i = 0; i < num_nt; ++i) {
	for (unsigned int bd = 0; bd < num_subgame_boards; ++bd) {
	  delete [] final_vals_[p1][i][bd];
	}
	delete [] final_vals_[p1][i];
      }
      delete [] final_vals_[p1];
    }
    delete [] final_vals_;
  }

  delete [] regret_floors_;
  delete [] regret_ceilings_;
  delete [] regret_scaling_;
  delete [] sumprob_scaling_;
  delete [] sumprob_streets_;
  delete [] compressed_streets_;
  delete [] best_response_streets_;
}

