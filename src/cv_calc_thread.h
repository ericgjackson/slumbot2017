#ifndef _CV_CALC_THREAD_H_
#define _CV_CALC_THREAD_H_

#include <string>

#include "vcfr.h"

using namespace std;

class BettingTree;
class Buckets;
class CFRConfig;
class CFRValues;
class HandTree;
class Node;

class CVCalcThread : public VCFR {
public:
  CVCalcThread(const CardAbstraction &ca, const BettingAbstraction &ba,
	       const CFRConfig &cc, const Buckets &buckets,
	       const BettingTree *betting_tree, unsigned int num_threads,
	       unsigned int it);
  ~CVCalcThread(void);
  void Go(Node *node, unsigned int bd);
private:
  void GetOppReachProbs(Node *node, unsigned int gbd, unsigned int opp,
			double *opp_probs);
  
  HandTree *hand_tree_;
  HandTree *prior_hand_tree_;
  unique_ptr<CFRValues> trunk_sumprobs_;
  unique_ptr<CFRValues> subgame_sumprobs_;
};

#endif
