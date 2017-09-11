#ifndef _ENDGAMES_H_
#define _ENDGAMES_H_

#include <vector>

#include "cards.h"
#include "eg_cfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFR;
class CFRConfig;
class CFRValues;
class HandTree;
class Node;

class EndgameSolver : public VCFR {
public:
  EndgameSolver(unsigned int solve_street, unsigned int base_it,
		const CardAbstraction &base_ca,
		const CardAbstraction &endgame_ca,
		const BettingAbstraction &base_ba,
		const BettingAbstraction &endgame_ba, const CFRConfig &base_cc,
		const CFRConfig &endgame_cc, const Buckets &base_buckets,
		const Buckets &endgame_buckets, BettingTree *base_betting_tree,
		ResolvingMethod method,	bool cfrs, bool card_level,
		bool zero_sum, unsigned int num_threads);
  ~EndgameSolver(void);
  void SolveSafe(Node *solve_root, Node *target_root, unsigned int solve_bd,
		 unsigned int target_bd, unsigned int base_solve_nt,
		 unsigned int base_target_nt, const vector<Node *> *base_path,
		 unsigned int num_its, bool p0, bool p1);
  void SolveUnsafe(Node *solve_root, Node *target_root, unsigned int solve_bd,
		   unsigned int target_bd, unsigned int base_solve_nt,
		   unsigned int base_target_nt,
		   const vector<Node *> *base_path, unsigned int num_its);
  void Solve(Node *solve_root, Node *target_root, unsigned int solve_bd,
	     unsigned int target_bd, unsigned int base_solve_nt,
	     unsigned int base_target_nt, const vector<Node *> *base_path,
	     unsigned int num_its, bool p0, bool p1);
  void Solve(Node *solve_root, Node *target_root, Node *base_solve_root,
	     unsigned int solve_bd, unsigned int target_bd,
	     unsigned int base_target_nt, unsigned int num_its,
	     BettingTree *endgame_betting_tree);
  bool GetPath(Node *base_node, Node *endgame_node, Node *base_target,
	       Node *endgame_target, vector<Node *> *rev_base_path);
  void BRGo(double *p0_br, double *p1_br);
private:
  void GetReachProbs(const vector<Node *> *base_path, const Card *board,
		     unsigned int *prior_bds, double **reach_probs);
  double *Process(Node *node, unsigned int lbd, double *opp_probs,
		  double sum_opp_probs, double *total_card_probs,
		  unsigned int last_st);

  unsigned int solve_street_;
  ResolvingMethod method_;
  unsigned int base_it_;
  unsigned int num_hole_card_pairs_;
  CFR *eg_cfr_;
  // Indexed by responder, player acting, nt, bd and hcp index
  double *****br_vals_;
  bool card_level_;
};

#endif
