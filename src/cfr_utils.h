#ifndef _CFR_UTILS_H_
#define _CFR_UTILS_H_

class CanonicalCards;
class CFRConfig;
class Node;

double *Showdown(Node *node, const CanonicalCards *hands, double *opp_probs,
		 double sum_opp_probs, double *total_card_probs);
double *Fold(Node *node, unsigned int p, const CanonicalCards *hands,
	     double *opp_probs, double sum_opp_probs,
	     double *total_card_probs);
void CommonBetResponseCalcs(unsigned int st,
			    const CanonicalCards *hands,
			    double *opp_probs,
			    double *sum_opp_probs,
			    double *total_card_probs);
void DeleteOldFiles(const CardAbstraction &ca, const BettingAbstraction &ba,
		    const CFRConfig &cc, unsigned int it);

// Normally we pass in regrets, but we can also pass in sumprobs.
template <class T>
void RegretsToProbs(T *regrets, unsigned int num_succs, bool nonneg,
		    bool uniform, unsigned int default_succ_index,
		    double explore, unsigned int num_nonterminal_succs,
		    bool *nonterminal_succs, double *probs) {
  double sum_regrets = 0;
  if (nonneg) {
    for (unsigned int s = 0; s < num_succs; ++s) {
      // We know all regrets are non-negative
      sum_regrets += regrets[s];
    }
    if (sum_regrets == 0) {
      if (uniform) {
	double uniform = 1.0 / (double)num_succs;
	for (unsigned int s = 0; s < num_succs; ++s) probs[s] = uniform;
      } else {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == default_succ_index) probs[s] = 1.0;
	  else                         probs[s] = 0;
	}
      }
    } else {
      for (unsigned int s = 0; s < num_succs; ++s) {
	// We know all regrets are non-negative
	probs[s] = regrets[s] / sum_regrets;
      }
    }
  } else {
    for (unsigned int s = 0; s < num_succs; ++s) {
      T r = regrets[s];
      if (r >= 0) sum_regrets += r;
    }
    if (sum_regrets == 0) {
      if (uniform) {
	double uniform = 1.0 / (double)num_succs;
	for (unsigned int s = 0; s < num_succs; ++s) probs[s] = uniform;
      } else {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == default_succ_index) probs[s] = 1.0;
	  else                         probs[s] = 0;
	}
      }
    } else {
      for (unsigned int s = 0; s < num_succs; ++s) {
	T r = regrets[s];
	if (r < 0) probs[s] = 0;
	else       probs[s] = r / sum_regrets;
      }
    }
  }
#if 0
  // Old code with no exception for terminal succs
  if (explore > 0) {
    double exploration = explore / num_succs;
    double dont_explore = 1.0 - explore;
    for (unsigned int s = 0; s < num_succs; ++s) {
      probs[s] *= dont_explore;
      probs[s] += exploration;
    }
  }
#endif
#if 0
  // Old code where each succs get a min of explore_ / num_succs
  if (explore > 0 && num_nonterminal_succs > 0) {
    double exploration = explore / num_nonterminal_succs;
    double dont_explore = 1.0 - explore;
    for (unsigned int s = 0; s < num_succs; ++s) {
      probs[s] *= dont_explore;
      if (nonterminal_succs[s]) probs[s] += exploration;
    }
  }
#endif
  // New code where each succs get a min of explore_
  if (explore > 0 && num_nonterminal_succs > 0) {
    double dont_explore = 1.0 - num_nonterminal_succs * explore;
    for (unsigned int s = 0; s < num_succs; ++s) {
      probs[s] *= dont_explore;
      if (nonterminal_succs[s]) probs[s] += explore;
    }
  }
}

#endif
