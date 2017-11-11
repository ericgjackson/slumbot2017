#ifndef _EG_CFR_H_
#define _EG_CFR_H_

#include <string>
#include <vector>

#include "vcfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CanonicalCards;
class CFRConfig;
class HandTree;
class Node;
class Reader;
class Writer;

enum class ResolvingMethod { UNSAFE, CFRD, MAXMARGIN, COMBINED };

class EGCFR : public VCFR {
 public:
  EGCFR(const CardAbstraction &ca, const CardAbstraction &base_ca,
	const BettingAbstraction &ba, const BettingAbstraction &base_ba,
	const CFRConfig &cc, const CFRConfig &base_cc, const Buckets &buckets,
	unsigned int solve_street, ResolvingMethod method, bool cfrs,
	bool zero_sum, unsigned int num_threads);
  virtual ~EGCFR(void);
  double *LoadOppCVs(Node *solve_root, const string &action_sequence,
		     unsigned int bd, unsigned int target_p,
		     unsigned int base_it, double **reach_probs,
		     const CanonicalCards *hands, bool card_level);
  void SolveSubgame(BettingTree *subtree, unsigned int solve_bd,
		    double **reach_probs, const string &action_sequence,
		    const HandTree *hand_tree, double *opp_cvs,
		    unsigned int target_p, bool both_players,
		    unsigned int num_its);
  void Write(BettingTree *subtree, Node *solve_root, Node *target_root,
	     const string &action_sequence, unsigned int num_its,
	     unsigned int target_bd);
  void Read(BettingTree *subtree, const string &action_sequence,
	    unsigned int subtree_bd, unsigned int target_st, bool both_players,
	    unsigned int it);
  double *BRGo(BettingTree *subtree, unsigned int solve_bd, unsigned int p,
	       double **reach_probs, HandTree *hand_tree,
	       const string &action_sequence);
  // Do I need this?  Using it in solve_all_endgames2.
  const CFRValues *Sumprobs(void) const {return sumprobs_.get();}
 private:
  double *HalfIteration(BettingTree *subtree, unsigned int solve_bd,
			unsigned int p, const VCFRState &state);
  void CFRDHalfIteration(BettingTree *subtree, unsigned int solve_bd,
			 unsigned int p, double *opp_cvs, VCFRState *state);
  void CombinedHalfIteration(BettingTree *subtree, unsigned int solve_bd,
			     unsigned int p, double *opp_reach_probs,
			     double *opp_cvs, VCFRState *state);
  void MaxMarginHalfIteration(BettingTree *subtree, unsigned int solve_bd,
			      unsigned int p, double *opp_cvs,
			      VCFRState *state);
  double *LoadCVs(Node *subtree_root, const string &action_sequence,
		  unsigned int gbd, unsigned int base_it, unsigned int p,
		  double **reach_probs, const CanonicalCards *hands,
		  bool card_level);
  double *LoadZeroSumCVs(Node *subtree_root, const string &action_sequence,
			 unsigned int gbd, unsigned int target_p,
			 unsigned int base_it, double **reach_probs,
			 const CanonicalCards *hands, bool card_level);
  void Write(Node *root, const string &action_sequence, unsigned int it,
	     unsigned int target_st, unsigned int target_bd);

  const CardAbstraction &base_card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
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
};

const char *ResolvingMethodName(ResolvingMethod method);
void FloorCVs(Node *subtree_root, double *opp_reach_probs,
	      const CanonicalCards *hands, double *cvs);
void ZeroSumCVs(double *p0_cvs, double *p1_cvs,
		unsigned int num_hole_card_pairs, double **reach_probs,
		const CanonicalCards *hands);

#endif
