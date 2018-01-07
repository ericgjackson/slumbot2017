#ifndef _CBR_THREAD_H_
#define _CBR_THREAD_H_

#include <string>

#include "vcfr.h"

using namespace std;

class BettingTree;
class Buckets;
class CFRConfig;
class CFRValues;
class HandTree;
class Node;

class CBRThread : public VCFR {
public:
  CBRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
	    const CFRConfig &cc, const Buckets &buckets,
	    const BettingTree *betting_tree, bool cfrs, unsigned int p,
	    HandTree *trunk_hand_tree, unsigned int num_threads,
	    unsigned int it);
  ~CBRThread(void);
  double Go(void);
private:
  
  void WriteValues(Node *node, unsigned int gbd, const string &action_sequence,
		   double *vals);
  double *OurChoice(Node *node, unsigned int lbd, const VCFRState &state);
  double *OppChoice(Node *node, unsigned int lbd, const VCFRState &state);

  bool cfrs_;
  unsigned int p_;
  HandTree *trunk_hand_tree_;
  unique_ptr<CFRValues> sumprobs_;
};

#endif
