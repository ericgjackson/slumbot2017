#ifndef _SAMPLED_BCFR_BUILDER_H_
#define _SAMPLED_BCFR_BUILDER_H_

#include <memory>
#include <string>

#include "vcfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;
class HandTree;
class Node;

class SampledBCFRBuilder : public VCFR {
public:
  SampledBCFRBuilder(const CardAbstraction &ca, const BettingAbstraction &ba,
		     const CFRConfig &cc, const Buckets &buckets,
		     const BettingTree *betting_tree, unsigned int p,
		     unsigned int it, unsigned int sample_st,
		     unsigned int num_to_sample, unsigned int num_threads);
  ~SampledBCFRBuilder(void);
  void Go(void);
  double *Process(Node *node, unsigned int lbd, const VCFRState &state,
		  unsigned int last_st, double **norms);
private:
  double *Showdown(Node *node, const CanonicalCards *hands, double *opp_probs,
		   double sum_opp_probs, double *total_card_probs,
		   double **ret_norms);
  double *Fold(Node *node, unsigned int p, const CanonicalCards *hands,
	       double *opp_probs, double sum_opp_probs,
	       double *total_card_probs, double **ret_norms);
  double *OurChoice(Node *node, unsigned int lbd, const VCFRState &state,
		    double **ret_norms);
  double *OppChoice(Node *node, unsigned int lbd, const VCFRState &state,
		    double **ret_norms);
  void Split(Node *node, double *opp_probs, const HandTree *hand_tree,
	     const string &action_sequence, unsigned int *prev_canons,
	     double *vals, double *norms);
  double *StreetInitial(Node *node, unsigned int plbd, 
			const VCFRState &state, double **ret_norms);
  void Write(Node *node, string *action_sequences);

  unsigned int p_;
  HandTree *trunk_hand_tree_;
  // Indexed by street, player acting, NT, and bucket/succ
  float ****bucket_sum_vals_;
  // Indexed by street, player acting, NT, and bucket/succ
  float ****bucket_denoms_;
  unsigned int sample_st_;
  unsigned int num_to_sample_;
  pthread_mutex_t ***mutexes_;
  unique_ptr<CFRValues> regrets_;
  unique_ptr<CFRValues> sumprobs_;
};

#endif
