#ifndef _CFR_H_
#define _CFR_H_

#include <memory>

#include "cfr_values.h"

using namespace std;

class BettingTree;
class CanonicalCards;
class CFRValues;
class HandTree;

class CFR {
 public:
  CFR(void) {}
  virtual ~CFR(void) {}
  virtual double *LoadOppCVs(Node *solve_root, unsigned int base_solve_nt,
			     unsigned int bd, unsigned int target_p,
			     unsigned int base_it, double **reach_probs,
			     const CanonicalCards *hands, bool card_level) {
    return nullptr;
  }
  virtual void SolveSubgame(BettingTree *subtree, unsigned int solve_bd,
			    double **reach_probs, HandTree *hand_tree,
			    double *opp_cvs, unsigned int target_p,
			    bool both_players, unsigned int num_its) {}
  virtual void Write(BettingTree *subtree, Node *solve_root, Node *target_root,
		     unsigned int base_target_nt, unsigned int num_its,
		     unsigned int target_bd) {}
  virtual void Read(BettingTree *subtree, unsigned int subtree_nt,
		    unsigned int subtree_bd, unsigned int target_st,
		    unsigned int it) {}
  virtual double *BRGo(BettingTree *subtree, unsigned int p,
		       double **reach_probs, HandTree *hand_tree) {
    return nullptr;
  }
 protected:
  unique_ptr<CFRValues> regrets_;
  unique_ptr<CFRValues> sumprobs_;
  unique_ptr<CFRValues> current_strategy_;
};

#endif
