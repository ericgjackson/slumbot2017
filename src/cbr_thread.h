#ifndef _CBR_THREAD_H_
#define _CBR_THREAD_H_

#include <string>

#include "vcfr.h"

using namespace std;

class BettingTree;
class Buckets;
class CFRConfig;
class HandTree;
class Node;

class CBRThread : public VCFR {
public:
  CBRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
	    const CFRConfig &cc, const Buckets &buckets,
	    const BettingTree *betting_tree, bool cfrs, unsigned int p,
	    HandTree *trunk_hand_tree, unsigned int thread_index,
	    unsigned int num_threads, unsigned int it, CBRThread **threads,
	    bool trunk);
  ~CBRThread(void);
  double Go(void);
  void AfterSplit(void);

  // Not supported yet
  static const unsigned int kSplitStreet = 999;
private:
  
  void WriteValues(Node *node, unsigned int gbd, const string &action_sequence,
		   double *vals);
  double *OurChoice(Node *node, unsigned int lbd, double *opp_probs,
		    double sum_opp_probs, double *total_card_probs,
		    unsigned int **street_buckets,
		    const string &action_sequence);
  double *OppChoice(Node *node, unsigned int lbd, double *opp_probs,
		    double sum_opp_probs, double *total_card_probs,
		    unsigned int **street_buckets,
		    const string &action_sequence);
  double *Split(Node *node, unsigned int bd, double *opp_probs);
  double *Process(Node *node, unsigned int lbd, double *opp_probs,
		  double sum_opp_probs, double *total_card_probs,
		  unsigned int **street_buckets, const string &action_sequence,
		  unsigned int last_st);
  void Run(void);
  void Join(void);
  void SetSplitNode(Node *n) {split_node_ = n;}
  void SetSplitBd(unsigned int bd) {split_bd_ = bd;}
  void SetOppReachProbs(double *p) {opp_reach_probs_ = p;}
  double *FinalHandVals(void) const {return final_hand_vals_;}

  bool cfrs_;
  HandTree *trunk_hand_tree_;
  unsigned int thread_index_;
  CBRThread **threads_;
  double *final_hand_vals_;
  Node *split_node_;
  unsigned int split_bd_;
  double *opp_reach_probs_;
  pthread_t pthread_id_;
};

#endif
