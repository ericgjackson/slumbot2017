#ifndef _VCFR_SUBGAME_H_
#define _VCFR_SUBGAME_H_

#include <string>

#include "vcfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class Node;

class VCFRSubgame : public VCFR {
public:
  VCFRSubgame(const CardAbstraction &ca, const BettingAbstraction &ba,
	      const CFRConfig &cc, const Buckets &buckets, Node *root,
	      unsigned int root_bd, const string &action_sequence, VCFR *cfr);
  ~VCFRSubgame(void);
  void Go(void);
  void SetIt(unsigned int it) {it_ = it;}
  void SetOppProbs(double *opp_probs);
  void SetThreadIndex(unsigned int t) {thread_index_ = t;}
  Node *Root(void) const {return root_;}
  unsigned int RootBd(void) const {return root_bd_;}
  double *FinalVals(void) const {return final_vals_;}
private:
  void DeleteOldFiles(unsigned int it);

  Node *root_;
  BettingTree *subtree_;
  VCFR *cfr_;
  bool *subtree_streets_;
  const string &action_sequence_;
  double *opp_probs_;
  unsigned int thread_index_;
  double *final_vals_;
};

#endif
