#ifndef _CFR_UTILS_H_
#define _CFR_UTILS_H_

#include "prob_method.h"

class BettingAbstraction;
class CanonicalCards;
class CardAbstraction;
class CFRConfig;
class Node;

double *Showdown(Node *node, const CanonicalCards *hands,
		 double *opp_probs, double sum_opp_probs,
		 double *total_card_probs);
double *Fold(Node *node, unsigned int p, const CanonicalCards *hands,
	     double *opp_probs, double sum_opp_probs,
	     double *total_card_probs);
void CommonBetResponseCalcs(unsigned int st,
			    const CanonicalCards *hands,
			    double *opp_probs,
			    double *sum_opp_probs,
			    double *total_card_probs);
void ProcessOppProbsBucketed(Node *node, unsigned int **street_buckets,
			     const CanonicalCards *hands,
			     bool nonneg, unsigned int it,
			     unsigned int soft_warmup,
			     unsigned int hard_warmup,
			     bool update_sumprobs, double *sumprob_scaling,
			     double *opp_probs, double **succ_opp_probs,
			     double *current_probs, int *sumprobs);
void ProcessOppProbsBucketed(Node *node, unsigned int **street_buckets,
			     const CanonicalCards *hands, bool nonneg,
			     unsigned int it, unsigned int soft_warmup,
			     unsigned int hard_warmup, bool update_sumprobs,
			     double *opp_probs, double **succ_opp_probs,
			     double *current_probs, double *sumprobs);
void ProcessOppProbs(Node *node, const CanonicalCards *hands, bool bucketed,
		     unsigned int **street_buckets, bool nonneg, bool uniform,
		     double explore, ProbMethod prob_method, unsigned int it,
		     unsigned int soft_warmup, unsigned int hard_warmup,
		     bool update_sumprobs, double *sumprob_scaling,
		     double *opp_probs, double **succ_opp_probs, int *cs_vals,
		     int *sumprobs);
void ProcessOppProbs(Node *node, const CanonicalCards *hands, bool bucketed,
		     unsigned int **street_buckets, bool nonneg, bool uniform,
		     double explore, unsigned int it, unsigned int soft_warmup,
		     unsigned int hard_warmup, bool update_sumprobs,
		     double *opp_probs, double **succ_opp_probs,
		     double *cs_vals, double *sumprobs);
void ProcessOppProbs(Node *node, const CanonicalCards *hands, bool bucketed,
		     unsigned int **street_buckets, bool nonneg, bool uniform,
		     double explore, unsigned int it, unsigned int soft_warmup,
		     unsigned int hard_warmup, bool update_sumprobs,
		     double *opp_probs, double **succ_opp_probs,
		     int *cs_vals, double *sumprobs);
void DeleteOldFiles(const CardAbstraction &ca, const BettingAbstraction &ba,
		    const CFRConfig &cc, unsigned int it);
void MPTerminal(unsigned int p, const CanonicalCards *hands,
		unsigned int *contributions, double **opp_probs,
		double **showdown_vals, double **fold_vals);

// Can pass in either regrets or sumprobs
template <class T>
void PureProbs(T *values, unsigned int num_succs, double *probs) {
  T max_v = values[0];
  unsigned int best_s = 0;
  for (unsigned int s = 1; s < num_succs; ++s) {
    T val = values[s];
    if (val > max_v) {
      max_v = val;
      best_s = s;
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    probs[s] = s == best_s ? 1.0 : 0.0;
  }
}

template <class T>
void FTLPureProbs(T *values, unsigned int num_succs, double *probs) {
  unsigned int s;
  for (s = 0; s < num_succs; ++s) {
    if (values[s] == 0) break;
  }
  if (s == num_succs) {
    fprintf(stderr, "No zero regret succ\n");
    for (s = 0; s < num_succs; ++s) {
      fprintf(stderr, "%i\n", (int)values[s]);
    }
    exit(-1);
  }
  for (unsigned int s1 = 0; s1 < num_succs; ++s1) {
    probs[s1] = s1 == s ? 1.0 : 0.0;
  }
}

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
