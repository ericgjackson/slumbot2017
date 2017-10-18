#ifndef _MP_VCFR_H_
#define _MP_VCFR_H_

#include <memory>

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;
class CFRValues;

class MPVCFR {
 public:
  MPVCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
	 const CFRConfig &cc, const Buckets &buckets,
	 const BettingTree *betting_tree, unsigned int num_threads);
 protected:
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  const BettingTree *betting_tree_;
  unique_ptr<CFRValues> regrets_;
  unique_ptr<CFRValues> sumprobs_;
  unique_ptr<CFRValues> current_strategy_;
  // best_response_ is true in run_rgbr, build_cbrs, build_prbrs
  // Whenever best_response_ is true, value_calculation_ is true
  bool *best_response_streets_;
  bool br_current_;
  // value_calculation_ is true in run_rgbr, build_cbrs, build_prbrs,
  // build_cfrs.
  bool value_calculation_;
  bool nn_regrets_;
  bool uniform_;
  unsigned int soft_warmup_;
  unsigned int hard_warmup_;
  double explore_;
  bool *sumprob_streets_;
  int *regret_floors_;
  double *sumprob_scaling_;
  unsigned int it_;
  unsigned int p_;
  HandTree *hand_tree_;
  
  double *OurChoice(Node *node, unsigned int lbd, unsigned int last_bet_to,
		    unsigned int *contributions, double **opp_probs,
		    unsigned int **street_buckets,
		    const string &action_sequence);
  double *OppChoice(Node *node, unsigned int lbd, unsigned int last_bet_to,
		    unsigned int *contributions, double **opp_probs,
		    unsigned int **street_buckets,
		    const string &action_sequence);
  double *Process(Node *node, unsigned int lbd, unsigned int last_bet_to,
		  unsigned int *contributions, double **opp_probs,
		  unsigned int **street_buckets, const string &action_sequence,
		  unsigned int last_st);
  
};

#endif
