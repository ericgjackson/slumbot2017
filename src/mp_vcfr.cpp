#include <stdio.h>
#include <stdlib.h>

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
#include "mp_vcfr.h"

MPVCFR::MPVCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	       const CFRConfig &cc, const Buckets &buckets,
	       const BettingTree *betting_tree, unsigned int num_threads) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(buckets), betting_tree_(betting_tree) {
}

double *MPVCFR::OurChoice(Node *node, unsigned int lbd,
			  unsigned int last_bet_to,
			  unsigned int *contributions, double **opp_probs,
			  unsigned int **street_buckets,
			  const string &action_sequence) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  double *vals = nullptr;
  double **succ_vals = new double *[num_succs];
  unsigned int csi = node->CallSuccIndex();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    unsigned int old_contribution = 0;
    if (s == csi) {
      old_contribution = contributions[p_];
      contributions[p_] = last_bet_to;
    }
    succ_vals[s] = Process(node->IthSucc(s), lbd, last_bet_to, contributions,
			   opp_probs, street_buckets, action_sequence + action,
			   st);
    if (s == csi) {
      contributions[p_] = old_contribution;
    }
  }
  if (num_succs == 1) {
    vals = succ_vals[0];
    succ_vals[0] = nullptr;
  } else {
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
    if (best_response_streets_[st]) {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	double max_val = succ_vals[0][i];
	for (unsigned int s = 1; s < num_succs; ++s) {
	  double sv = succ_vals[s][i];
	  if (sv > max_val) max_val = sv;
	}
	vals[i] = max_val;
      }
    } else {
      exit(-1);
    }
  }
  return vals;
}

double *MPVCFR::OppChoice(Node *node, unsigned int lbd,
			  unsigned int last_bet_to,
			  unsigned int *contributions, double **opp_probs,
			  unsigned int **street_buckets,
			  const string &action_sequence) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int pa = node->PlayerActing();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  const CanonicalCards *hands = hand_tree_->Hands(st, lbd);
  unsigned int num_players = Game::NumPlayers();
#if 0
  double ***succ_opp_probs = new double **[num_succs];
  if (num_succs == 1) {
    succ_opp_probs[0] = opp_probs;
  } else {
    unsigned int nt = node->NonterminalID();
    unsigned int num_hole_cards = Game::NumCardsForStreet(0);
    unsigned int max_card1 = Game::MaxCard() + 1;
    unsigned int num_enc;
    if (num_hole_cards == 1) num_enc = max_card1;
    else                     num_enc = max_card1 * max_card1;
    for (unsigned int s = 0; s < num_succs; ++s) {
      succ_opp_probs[s] = new double *[num_players];
      for (unsigned int p = 0; p < num_players; ++p) {
	if (p == pa) {
	  succ_opp_probs[s][p] = new double[num_enc];
	  for (unsigned int i = 0; i < num_enc; ++i) {
	    succ_opp_probs[s][p][i] = 0;
	  }
	} else {
	  succ_opp_probs[s][p] = opp_probs[p];
	}
      }
    }
#endif
    
  double **pa_succ_opp_probs = new double *[num_succs];
  if (num_succs == 1) {
    pa_succ_opp_probs[0] = opp_probs[pa];
  } else {
    unsigned int nt = node->NonterminalID();
    unsigned int num_hole_cards = Game::NumCardsForStreet(0);
    unsigned int max_card1 = Game::MaxCard() + 1;
    unsigned int num_enc;
    if (num_hole_cards == 1) num_enc = max_card1;
    else                     num_enc = max_card1 * max_card1;
    for (unsigned int s = 0; s < num_succs; ++s) {
      pa_succ_opp_probs[s] = new double[num_enc];
      for (unsigned int i = 0; i < num_enc; ++i) pa_succ_opp_probs[s][i] = 0;
    }

    // The "all" values point to the values for all hands.
    double *d_all_current_probs = nullptr;
    double *d_all_cs_vals = nullptr;
    int *i_all_cs_vals = nullptr;
    
    double explore;
    if (value_calculation_ && ! br_current_) explore = 0;
    else                                     explore = explore_;

    bool bucketed = ! buckets_.None(st) &&
      node->PotSize() < card_abstraction_.BucketThreshold(st);

    if (bucketed) {
      current_strategy_->Values(pa, st, nt, &d_all_current_probs);
    } else {
      // cs_vals are the current strategy values; i.e., the values we pass into
      // RegretsToProbs() in order to get the current strategy.  In VCFR, these
      // are regrets; in a best-response calculation, they are (normally)
      // sumprobs.

      if (value_calculation_ && ! br_current_) {
	if (sumprobs_->Ints(pa, st)) {
	  sumprobs_->Values(pa, st, nt, &i_all_cs_vals);
	} else {
	  sumprobs_->Values(pa, st, nt, &d_all_cs_vals);
	}
      } else {
	if (regrets_->Ints(pa, st)) {
	  regrets_->Values(pa, st, nt, &i_all_cs_vals);
	} else {
	  regrets_->Values(pa, st, nt, &d_all_cs_vals);
	}
      }
    }

    // The "all" values point to the values for all hands.
    double *d_all_sumprobs = nullptr;
    int *i_all_sumprobs = nullptr;
    // sumprobs_->Players(pa) check is there because in asymmetric systems
    // (e.g., endgame solving with CFR-D method) we are only saving probs for
    // one player.
    if (! value_calculation_ && sumprob_streets_[st] &&
	sumprobs_->Players(pa)) {
      if (sumprobs_->Ints(pa, st)) {
	sumprobs_->Values(pa, st, nt, &i_all_sumprobs);
      } else {
	sumprobs_->Values(pa, st, nt, &d_all_sumprobs);
      }
    }

    // These values will point to the values for the current board
    double *d_cs_vals = nullptr, *d_sumprobs = nullptr;
    int *i_cs_vals = nullptr, *i_sumprobs = nullptr;

    if (bucketed) {
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
    if (bucketed) {
      if (d_sumprobs) {
	// Double sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || d_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbsBucketed(node, street_buckets, hands, nonneg, it_,
				soft_warmup_, hard_warmup_, update_sumprobs,
				opp_probs[pa], pa_succ_opp_probs,
				d_all_current_probs, d_sumprobs);
      } else {
	// Int sumprobs
	bool update_sumprobs =
	  ! (value_calculation_ || i_sumprobs == nullptr ||
	     (hard_warmup_ > 0 && it_ <= hard_warmup_));
	ProcessOppProbsBucketed(node, street_buckets, hands, nonneg, it_,
				soft_warmup_, hard_warmup_, update_sumprobs,
				sumprob_scaling_, opp_probs[pa],
				pa_succ_opp_probs, d_all_current_probs,
				i_sumprobs);
      }
    } else {
      if (i_cs_vals) {
	if (d_sumprobs) {
	  // Int regrets, double sumprobs
	  bool update_sumprobs =
	    ! (value_calculation_ || d_sumprobs == nullptr ||
	       (hard_warmup_ > 0 && it_ <= hard_warmup_));
	  ProcessOppProbs(node, hands, nonneg, uniform_, explore, it_,
			  soft_warmup_, hard_warmup_, update_sumprobs,
			  opp_probs[pa], pa_succ_opp_probs, i_cs_vals,
			  d_sumprobs);
	} else {
	  // Int regrets and sumprobs
	  bool update_sumprobs =
	    ! (value_calculation_ || i_sumprobs == nullptr ||
	       (hard_warmup_ > 0 && it_ <= hard_warmup_));
	  ProcessOppProbs(node, hands, nonneg, uniform_, explore, it_,
			  soft_warmup_, hard_warmup_, update_sumprobs,
			  sumprob_scaling_, opp_probs[pa], pa_succ_opp_probs,
			  i_cs_vals, i_sumprobs);
	}
      } else {
	// Double regrets and sumprobs
	  bool update_sumprobs =
	    ! (value_calculation_ || d_sumprobs == nullptr ||
	       (hard_warmup_ > 0 && it_ <= hard_warmup_));
	  ProcessOppProbs(node, hands, nonneg, uniform_, explore, it_,
			  soft_warmup_, hard_warmup_, update_sumprobs,
			  opp_probs[pa], pa_succ_opp_probs, d_cs_vals,
			  d_sumprobs);
      }
    }
  }

  double *vals = nullptr;
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    double **succ_opp_probs = new double *[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      if (p == pa) {
	succ_opp_probs[p] = pa_succ_opp_probs[s];
      } else {
	succ_opp_probs[p] = opp_probs[p];
      }
    }
    double *succ_vals = Process(node->IthSucc(s), lbd, last_bet_to,
				contributions, succ_opp_probs,
				street_buckets, action_sequence + action, st);
    delete [] succ_opp_probs;
    if (vals == nullptr) {
      vals = succ_vals;
    } else {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	vals[i] += succ_vals[i];
      }
      delete [] succ_vals;
    }
  }
  if (vals == nullptr) {
    // This can happen if there were non-zero opp probs on the prior street,
    // but the board cards just dealt blocked all the opponent hands with
    // non-zero probability.
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
  }
  
  if (num_succs > 1) {
    for (unsigned int s = 0; s < num_succs; ++s) {
      delete [] pa_succ_opp_probs[s];
    }
  }
  delete [] pa_succ_opp_probs;

  return vals;
}

double *MPVCFR::Process(Node *node, unsigned int lbd, unsigned int last_bet_to,
			unsigned int *contributions, double **opp_probs,
			unsigned int **street_buckets,
			const string &action_sequence, unsigned int last_st) {
  unsigned int st = node->Street();
  if (node->Terminal()) {
    double *showdown_vals, *fold_vals;
    MPTerminal(p_, hand_tree_->Hands(st, lbd), contributions, opp_probs,
	       &showdown_vals, &fold_vals);
    if (node->NumRemaining() == 1) {
      delete [] showdown_vals;
      return fold_vals;
    } else {
      delete [] fold_vals;
      return showdown_vals;
    }
  } else {
    if (node->PlayerActing() == p_) {
      return OurChoice(node, lbd, last_bet_to, contributions, opp_probs,
		       street_buckets, action_sequence);
    } else {
      return OppChoice(node, lbd, last_bet_to, contributions, opp_probs,
		       street_buckets, action_sequence);
    }
  }
#if 0
  if (st > last_st) {
    return StreetInitial(node, lbd, opp_probs, street_buckets,
			 action_sequence);
  }
#endif
}

