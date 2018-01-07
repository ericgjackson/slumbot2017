#ifndef _DYNAMIC_CBR2_H_
#define _DYNAMIC_CBR2_H_

#include "vcfr.h"

class Buckets;
class CanonicalCards;
class CardAbstraction;
class CFRValues;
class HandTree;
class Node;

class DynamicCBR2 : public VCFR {
public:
  DynamicCBR2(const CardAbstraction &ca, const BettingAbstraction &ba,
	      const CFRConfig &cc, const Buckets &buckets,
	      unsigned int num_threads);
  DynamicCBR2(void);
  ~DynamicCBR2(void);
  double *Compute(Node *node, double **reach_probs, unsigned int gbd,
		  HandTree *hand_tree, unsigned int root_bd_st,
		  unsigned int root_bd, unsigned int target_p, bool cfrs,
		  bool zero_sum, bool current, bool purify_opp,
		  CFRValues *regrets, CFRValues *sumprobs);
private:
  double *Compute(Node *node, unsigned int p, double *opp_probs,
		  unsigned int gbd, HandTree *hand_tree,
		  unsigned int root_bd_st, unsigned int root_bd,
		  CFRValues *regrets, CFRValues *sumprobs);

  bool cfrs_;
};

#endif
