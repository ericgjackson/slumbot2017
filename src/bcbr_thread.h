#ifndef _BCBR_THREAD_H_
#define _BCBR_THREAD_H_

#include <vector>

#include "vcfr.h"

using namespace std;

class BettingTree;
class Buckets;
class CFRConfig;
class HandTree;
class Node;

class BCBRThread : public VCFR {
public:
  BCBRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
	     const CFRConfig &cc, const Buckets &buckets,
	     const BettingTree *betting_tree, unsigned int p,
	     HandTree *trunk_hand_tree, unsigned int thread_index,
	     unsigned int num_threads, unsigned int it, BCBRThread **threads,
	     bool trunk);
  ~BCBRThread(void);
  void Go(void);
  void AfterSplit(void);

  // Not supported yet
  static const unsigned int kSplitStreet = 999;
private:
  
  void WriteValues(Node *node, unsigned int gbd, bool alt,
		   const string &action_sequence, double *vals);
  double *OurChoice(Node *node, unsigned int lbd, const VCFRState &state);
  double *OppChoice(Node *node, unsigned int lbd, const VCFRState &state);
  double *Process(Node *node, unsigned int lbd, const VCFRState &state,
		  unsigned int last_st);
  void CardPass(bool first_pass);

  double *SecondPassOurChoice(Node *node);
  double *SecondPassOppChoice(Node *node);
  double *SecondPass(Node *node, unsigned int last_st);

  unsigned int p_;
  HandTree *trunk_hand_tree_;
  unsigned int thread_index_;
  BCBRThread **threads_;
  Node *split_node_;
  unsigned int split_bd_;
  double *opp_reach_probs_;
  // Doesn't support multithreading yet
  double **terminal_bucket_vals_;
  double ***si_bucket_vals_;
  // Indexed by street, nt and bucket
  unsigned char ***best_succs_;
  bool first_pass_;
  unsigned int target_st_;
  unique_ptr<CFRValues> sumprobs_;
};

#endif
