#ifndef _EG_CFR_H_
#define _EG_CFR_H_

#include <string>
#include <vector>

#include "resolving_method.h"
#include "vcfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CanonicalCards;
class CFRConfig;
class CFRValues;
class HandTree;
class Node;
class Reader;
class Writer;

class EGCFR : public VCFR {
 public:
  EGCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	const CFRConfig &cc, const Buckets &buckets, unsigned int solve_street,
	ResolvingMethod method, bool cfrs, bool zero_sum,
	unsigned int num_threads);
  virtual ~EGCFR(void);
  double *LoadOppCVs(Node *solve_root, const string &action_sequence,
		     unsigned int bd, unsigned int target_p,
		     unsigned int base_it, double **reach_probs,
		     const CanonicalCards *hands, bool card_level,
		     const CardAbstraction &base_card_abstraction,
		     const BettingAbstraction &base_betting_abstraction,
		     const CFRConfig &base_cfr_config);
  void SolveSubgame(BettingTree *subtree, unsigned int solve_bd,
		    double **reach_probs, const string &action_sequence,
		    const HandTree *hand_tree, double *opp_cvs,
		    unsigned int target_p, bool both_players,
		    unsigned int num_its, CFRValues *sumprobs);
  void Write(BettingTree *subtree, Node *solve_root, Node *target_root,
	     const string &action_sequence,
	     const CardAbstraction &base_card_abstraction,
	     const BettingAbstraction &base_betting_abstraction,
	     const CFRConfig &base_cfr_config, unsigned int num_its,
	     unsigned int target_bd, CFRValues *sumprobs);
  void Read(BettingTree *subtree, const string &action_sequence,
	    const CardAbstraction &base_card_abstraction,
	    const BettingAbstraction &base_betting_abstraction,
	    const CFRConfig &base_cfr_config, unsigned int subtree_bd,
	    unsigned int target_st, bool both_players, unsigned int it,
	    CFRValues *sumprobs);
  double *BRGo(BettingTree *subtree, unsigned int solve_bd, unsigned int p,
	       double **reach_probs, HandTree *hand_tree,
	       const string &action_sequence, CFRValues *sumprobs);
 private:
  double *HalfIteration(BettingTree *subtree, unsigned int solve_bd,
			const VCFRState &state);
  void CFRDHalfIteration(BettingTree *subtree, unsigned int solve_bd,
			 double *opp_cvs, VCFRState *state);
  void CombinedHalfIteration(BettingTree *subtree, unsigned int solve_bd,
			     double **reach_probs, double *opp_cvs,
			     VCFRState *state);
  void MaxMarginHalfIteration(BettingTree *subtree, unsigned int solve_bd,
			      double *opp_cvs, VCFRState *state);
  double *LoadCVs(Node *subtree_root, const string &action_sequence,
		  unsigned int gbd, unsigned int base_it, unsigned int p,
		  double **reach_probs, const CanonicalCards *hands,
		  bool card_level,
		  const CardAbstraction &base_card_abstraction,
		  const BettingAbstraction &base_betting_abstraction,
		  const CFRConfig &base_cfr_config);
  double *LoadZeroSumCVs(Node *subtree_root, const string &action_sequence,
			 unsigned int gbd, unsigned int target_p,
			 unsigned int base_it, double **reach_probs,
			 const CanonicalCards *hands, bool card_level,
			 const CardAbstraction &base_card_abstraction,
			 const BettingAbstraction &base_betting_abstraction,
			 const CFRConfig &base_cfr_config);
  void Write(Node *target_root, const string &action_sequence,
	     const CardAbstraction &base_card_abstraction,
	     const BettingAbstraction &base_betting_abstraction,
	     const CFRConfig &base_cfr_config, unsigned int it,
	     unsigned int target_st, unsigned int target_bd,
	     CFRValues *sumprobs);

  unsigned int subtree_st_;
  ResolvingMethod method_;
  bool cfrs_;
  bool zero_sum_;
  unsigned int num_threads_;
  double *cfrd_regrets_;
  double *maxmargin_regrets_;
  double *combined_regrets_;
  double **symmetric_combined_regrets_;
  const HandTree *hand_tree_;
#if 0
  // The sum of target player probs for each opponent hand.
  double *sum_target_probs_;
#endif
  unique_ptr<CFRValues> regrets_;
};

#endif
