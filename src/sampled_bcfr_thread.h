#ifndef _SAMPLED_BCFR_THREAD_H_
#define _SAMPLED_BCFR_THREAD_H_

#include <memory>
#include <string>

#include "vcfr.h"

using namespace std;

class BettingTree;
class Buckets;
class CFRConfig;
class HandTree;
class Node;

class SampledBCFRThread : public VCFR {
public:
  SampledBCFRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
		    const CFRConfig &cc, const Buckets &buckets,
		    const BettingTree *betting_tree, unsigned int p,
		    HandTree *trunk_hand_tree, unsigned int thread_index,
		    unsigned int num_threads, unsigned int it,
		    unsigned int sample_st, unsigned int num_to_sample,
		    double *****bucket_sum_vals,
		    unsigned int ****bucket_counts,
		    SampledBCFRThread **threads, bool trunk);
  ~SampledBCFRThread(void);
  void Go(void);
  void Run(void);
  void Join(void);
  void AfterSplit(void);
  void SetSplitNode(Node *node) {split_node_ = node;}
  void SetSplitOppProbs(double *opp_probs) {split_opp_probs_ = opp_probs;}
  void SetSplitActionSequence(const string &s) {split_action_sequence_ = s;}
  void SetSplitPrevCanons(unsigned int *pc) {split_prev_canons_ = pc;}
  double *SplitVals(void) const {return split_vals_;}
private:

  double *OurChoice(Node *node, unsigned int lbd, double *opp_probs,
		    double sum_opp_probs, double *total_card_probs,
		    unsigned int **street_buckets,
		    const string &action_sequence);
  double *StreetInitial(Node *node, unsigned int plbd, double *opp_probs,
			unsigned int **street_buckets,
			const string &action_sequence);

  HandTree *trunk_hand_tree_;
  unsigned int sample_st_;
  unsigned int num_to_sample_;
  unique_ptr<unsigned int []> board_counts_;
  unsigned int thread_index_;
  SampledBCFRThread **threads_;
  pthread_t pthread_id_;
  Node *split_node_;
  double *split_vals_;
  double *split_opp_probs_;
  string split_action_sequence_;
  unsigned int *split_prev_canons_;
  // Indexed by street, player acting, NT, bucket and succ
  double *****bucket_sum_vals_;
  unsigned int ****bucket_counts_;
};

#endif
