#ifndef _PCS_CFR_H_
#define _PCS_CFR_H_

#include "cfr.h"

#include <memory>

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;
class CFRValues;
class HandTree;

using namespace std;

class PCSCFR : public CFR {
public:
  PCSCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	 const CFRConfig &cc, const Buckets &buckets,
	 unsigned int num_threads);
  virtual ~PCSCFR(void);
  void Run(unsigned int start_it, unsigned int end_it);
 protected:
  double *OurChoice(Node *node, unsigned int msbd, double *opp_probs,
		    double sum_opp_probs, double *total_card_probs);
  double *OppChoice(Node *node, unsigned int msbd, double *opp_probs,
		    double sum_opp_probs, double *total_card_probs);
  double *Process(Node *node, unsigned int msbd, double *opp_probs,
		  double sum_opp_probs, double *total_card_probs,
		  unsigned int last_st);
  void HalfIteration(BettingTree *betting_tree, unsigned int p,
		     double *opp_probs);
  void ReadFromCheckpoint(unsigned int it);
  void Checkpoint(unsigned int it);

  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  BettingTree *betting_tree_;
  HandTree *hand_tree_;
  double explore_;
  bool uniform_;
  bool *compressed_streets_;
  unsigned int *board_buckets_;
  unsigned int board_count_;
  unsigned int it_;
  unsigned int p_;
  unsigned int initial_max_street_board_;
  unsigned int end_max_street_boards_;
};

#if 0
class EGPCSCFR : public PCSCFR {
 public:
  EGPCSCFR(const CardAbstraction &ca, const CardAbstraction &base_ca,
	   const BettingAbstraction &ba, const BettingAbstraction &base_ba,
	   const CFRConfig &cc, const CFRConfig &base_cc,
	   const Buckets &buckets, unsigned int num_threads);
  ~EGPCSCFR(void) {}
  void SolveSubgame(unsigned int root_bd, BettingTree *subtree,
		    double **reach_probs, HandTree *hand_tree,
		    unsigned int base_subtree_nt, unsigned int base_it,
		    unsigned int target_p, unsigned int num_its);
 private:
  void WriteSubgame(BettingTree *subtree, unsigned int subtree_nt,
		    unsigned int it);
  
  const CardAbstraction &base_card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
};
#endif

#endif
