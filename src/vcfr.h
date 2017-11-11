#ifndef _VCFR_H_
#define _VCFR_H_

#include <semaphore.h>

#include <memory>
#include <string>

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CanonicalCards;
class CardAbstraction;
class CFRConfig;
class CFRValues;
class HandTree;
class Node;
class VCFRSubgame;

class VCFRState {
 public:
  VCFRState(double *opp_probs, unsigned int **street_buckets,
	    const HandTree *hand_tree);
  VCFRState(double *opp_probs, const HandTree *hand_tree, unsigned int st,
	    unsigned int lbd, const string &action_sequence,
	    unsigned int root_bd, unsigned int root_bd_st,
	    unsigned int **street_buckets);
  VCFRState(double *opp_probs, double *total_card_probs,
	    const HandTree *hand_tree, unsigned int st, unsigned int lbd,
	    const string &action_sequence, unsigned int root_bd,
	    unsigned int root_bd_st, unsigned int **street_buckets);
  VCFRState(const VCFRState &pred, Node *node, unsigned int s);
  VCFRState(const VCFRState &pred, Node *node, unsigned int s,
	    double *opp_probs, double sum_opp_probs, double *total_card_probs);
  virtual ~VCFRState(void);
  double *OppProbs(void) const {return opp_probs_;}
  double SumOppProbs(void) const {return sum_opp_probs_;}
  double *TotalCardProbs(void) const {return total_card_probs_;}
  unsigned int **StreetBuckets(void) const {return street_buckets_;}
  const string &ActionSequence(void) const {return action_sequence_;}
  const HandTree *GetHandTree(void) const {return hand_tree_;}
  unsigned int RootBd(void) const {return root_bd_;}
  unsigned int RootBdSt(void) const {return root_bd_st_;}
  void SetOppProbs(double *opp_probs) {opp_probs_ = opp_probs;}
 protected:
  double *opp_probs_;
  double sum_opp_probs_;
  double *total_card_probs_;
  unsigned int **street_buckets_;
  string action_sequence_;
  const HandTree *hand_tree_;
  unsigned int root_bd_;
  unsigned int root_bd_st_;
};

class VCFR {
 public:
  VCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
       const CFRConfig &cc, const Buckets &buckets,
       const BettingTree *betting_tree, unsigned int num_threads);
  virtual ~VCFR(void);
  virtual double *Process(Node *node, unsigned int lbd, const VCFRState &state,
			  unsigned int last_st);
  virtual void SetStreetBuckets(unsigned int st, unsigned int gbd,
				const VCFRState &state);
  void SetIt(unsigned int it) {it_ = it;}
  void SetLastCheckpointIt(unsigned int it) {last_checkpoint_it_ = it;}
  void SetP(unsigned int p) {p_ = p;}
  void SetTargetP(unsigned int p) {target_p_ = p;}
  void SetBestResponseStreets(bool *sts);
  void SetBRCurrent(bool b) {br_current_ = b;}
  void SetValueCalculation(bool b) {value_calculation_ = b;}
  virtual void Post(unsigned int t);
  const Buckets &GetBuckets(void) const {return buckets_;}
 protected:
  const unsigned int kMaxDepth = 100;
  
  virtual void UpdateRegrets(Node *node, double *vals, double **succ_vals,
			     int *regrets);
  virtual void UpdateRegrets(Node *node, double *vals, double **succ_vals,
			     double *regrets);
  virtual void UpdateRegretsBucketed(Node *node, unsigned int **street_buckets,
				     double *vals, double **succ_vals,
				     int *regrets);
  virtual void UpdateRegretsBucketed(Node *node, unsigned int **street_buckets,
				     double *vals, double **succ_vals,
				     double *regrets);
  virtual double *OurChoice(Node *node, unsigned int lbd,
			    const VCFRState &state);
  virtual double *OppChoice(Node *node, unsigned int lbd, 
			    const VCFRState &state);
  virtual void Split(Node *node, double *opp_probs, const HandTree *hand_tree,
		     const string &action_sequence, unsigned int *prev_canons,
		     double *vals);
  virtual double *StreetInitial(Node *node, unsigned int lbd,
				const VCFRState &state);
  virtual void WaitForFinalSubgames(void);
  virtual void SpawnSubgame(Node *node, unsigned int bd,
			    const string &action_sequence, double *opp_probs);
  virtual void SetCurrentStrategy(Node *node);

  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  const BettingTree *betting_tree_;
  unique_ptr<CFRValues> regrets_;
  unique_ptr<CFRValues> sumprobs_;
  unique_ptr<CFRValues> current_strategy_;
  bool subgame_;
  // best_response_ is true in run_rgbr, build_cbrs, build_prbrs
  // Whenever best_response_ is true, value_calculation_ is true
  bool *best_response_streets_;
  bool br_current_;
  // value_calculation_ is true in run_rgbr, build_cbrs, build_prbrs,
  // build_cfrs.
  bool value_calculation_;
  bool prune_;
  bool always_call_preflop_;
  unsigned int target_p_;
  unsigned int num_players_;
  unsigned int subgame_street_;
  unsigned int split_street_;
  bool nn_regrets_;
  bool uniform_;
  unsigned int soft_warmup_;
  unsigned int hard_warmup_;
  double explore_;
  bool double_regrets_;
  bool double_sumprobs_;
  bool *compressed_streets_;
  bool *sumprob_streets_;
  int *regret_floors_;
  int *regret_ceilings_;
  double *regret_scaling_;
  double *sumprob_scaling_;
  unsigned int it_;
  unsigned int last_checkpoint_it_;
  unsigned int p_;
  bool bucketed_; // Does at least one street have buckets?
  bool pre_phase_;
  double ****final_vals_;
  bool *subgame_running_;
  pthread_t *pthread_ids_;
  VCFRSubgame **active_subgames_;
  sem_t available_;
  unsigned int num_threads_;
};

unsigned int **AllocateStreetBuckets(void);
void DeleteStreetBuckets(unsigned int **street_buckets);
double *AllocateOppProbs(bool initialize);
void DeleteOldFiles(const CardAbstraction &ca, const BettingAbstraction &ba,
		    const CFRConfig &cc, unsigned int it);

#endif
