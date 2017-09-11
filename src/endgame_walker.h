#ifndef _ENDGAME_WALKER_H_
#define _ENDGAME_WALKER_H_

#include "cfr_values.h"
#include "eg_cfr.h" // ResolvingMethod

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;
class HandTree;

class EndgameWalker {
public:
  EndgameWalker(unsigned int solve_st, unsigned int base_it,
		const CardAbstraction &base_ca,
		const CardAbstraction &endgame_ca,
		const BettingAbstraction &base_ba,
		const BettingAbstraction &endgame_ba, const CFRConfig &base_cc,
		const CFRConfig &endgame_cc, const Buckets &base_buckets,
		const Buckets &endgame_buckets, BettingTree *base_betting_tree,
		BettingTree *endgame_betting_tree, ResolvingMethod method,
		bool cfrs, bool zero_sum, unsigned int num_its,
		unsigned int num_threads);
  ~EndgameWalker(void);
  void Go(void);
private:
  void ProcessEndgame(Node *base_node, Node *endgame_node, unsigned int bd,
		      double **reach_probs);
  void CalculateSuccProbs(Node *node, unsigned int bd, double **reach_probs,
			  double ****new_reach_probs);
  void InitializeStreetBuckets(unsigned int st, unsigned int bd);
  void StreetInitial(Node *base_node, Node *endgame_node, unsigned int pbd,
		     double **reach_probs);
  void Process(Node *base_node, Node *endgame_node, unsigned int bd,
	       double **reach_probs, unsigned int last_st);

  const CardAbstraction &base_card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
  const Buckets &base_buckets_;
  unsigned int solve_st_;
  unsigned int base_it_;
  BettingTree *base_betting_tree_;
  BettingTree *endgame_betting_tree_;
  unsigned int num_its_;
  EGCFR *eg_cfr_;
  HandTree *hand_tree_;
  unique_ptr<CFRValues> sumprobs_;
  unsigned int **street_buckets_;
  double *sum_all_endgame_norms_;
  double *sum_sampled_endgame_norms_;
  double *sum_br_vals_;
};

#endif
