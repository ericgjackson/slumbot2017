#ifndef _TCFR_H_
#define _TCFR_H_

#include <memory>

#include "cfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;
class Node;
class Reader;
class Writer;

#define T_REGRET unsigned int
#define T_VALUE int
#define T_SUM_PROB unsigned int

#define SUCCPTR(ptr) (ptr + 8)

static const unsigned int kNumPregenRNGs = 10000000;

class TCFRThread {
public:
  TCFRThread(const BettingAbstraction &ba, const CFRConfig &cc,
	     const Buckets &buckets, unsigned int batch_index,
	     unsigned int num_threads, unsigned char *data,
	     unsigned int target_player, float *rngs, unsigned int *uncompress,
	     unsigned int *short_uncompress, unsigned int *pruning_thresholds,
	     bool **sumprob_streets, unsigned char *hvb_table,
	     unsigned char ***cards_to_indices, unsigned int num_raw_boards,
	     const unsigned int *board_table_, unsigned int batch_size,
	     unsigned long long int *total_its);
  virtual ~TCFRThread(void);
  void RunThread(void);
  void Join(void);
  void Run(void);
  unsigned int BatchIndex(void) const {return batch_index_;}
  unsigned long long int ProcessCount(void) const {return process_count_;}
  unsigned long long int FullProcessCount(void) const {
    return full_process_count_;
  }
 protected:
  static const unsigned int kStackDepth = 500;
  static const unsigned int kMaxSuccs = 50;

  virtual T_VALUE Process(unsigned char *ptr, unsigned int last_player_acting,
			  int last_st, bool adjust);
  void HVBDealHand(void);
  void NoHVBDealHand(void);
  int Round(double d);

  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  unsigned int batch_index_;
  unsigned int num_threads_;
  unsigned char *data_;
  bool asymmetric_;
  bool boost_;
  bool maintain_cvs_;
  unsigned int num_players_;
  unsigned int target_player_;
  unsigned int p_;
  unsigned int *winners_;
  // Keep this as a signed int so we can use it in winnings calculation
  // without casting.
  int *contributions_;
  bool *folded_;
  unsigned int *canon_bds_;
  unsigned int *hi_cards_;
  unsigned int *lo_cards_;
  unsigned int *hole_cards_;
  unsigned int *hvs_;
  unsigned int *hand_buckets_;
  pthread_t pthread_id_;
  T_VALUE **succ_value_stack_;
  int **succ_iregret_stack_;
  unsigned int stack_index_;
  double explore_;
  unsigned int *sumprob_ceilings_;
  unsigned long long int it_;
  float *rngs_;
  unsigned int rng_index_;
  unique_ptr<bool []> char_quantized_streets_;
  unique_ptr<bool []> short_quantized_streets_;
  bool *scaled_streets_;
  bool full_only_avg_update_;
  unsigned int *uncompress_;
  unsigned int *short_uncompress_;
  unsigned int *pruning_thresholds_;
  bool **sumprob_streets_;
  unsigned char *hvb_table_;
  unsigned long long int bytes_per_hand_;
  unsigned char ***cards_to_indices_;
  unsigned int num_raw_boards_;
  const unsigned int *board_table_;
  unsigned int max_street_;
  bool all_full_;
  bool *full_;
  unique_ptr<unsigned int []> close_thresholds_;
  unsigned long long int process_count_;
  unsigned long long int full_process_count_;
  unsigned int active_mod_;
  unsigned int num_active_conditions_;
  unsigned int *num_active_streets_;
  unsigned int *num_active_rems_;
  unsigned int **active_streets_;
  unsigned int **active_rems_;
  unsigned int batch_size_;
  unsigned long long int *total_its_;
  struct drand48_data rand_buf_;
  // Keep this as a signed int so we can use it in winnings calculation
  // without casting.
  int board_count_;
  bool deal_twice_;
  int **force_regrets_;
};

class TCFR : public CFR {
public:
  TCFR(const CardAbstraction &ca, const BettingAbstraction &ba,
       const CFRConfig &cc, const Buckets &buckets, unsigned int num_threads,
       unsigned int target_player);
  ~TCFR(void);
  void Run(unsigned int start_batch_base, unsigned int batch_size,
	   unsigned int save_interval);
  void Run(unsigned int start_batch_base, unsigned int end_batch_base,
	   unsigned int batch_size, unsigned int save_interval);
  // In extract_cvs
  void Extract(unsigned int it);
private:
  void ReadCVs(unsigned char *ptr, Node *node, Reader ***readers,
	       bool ***seen);
  void WriteCVs(unsigned char *ptr, Node *node, Writer ***writers,
		bool ***seen);
  void ReadRegrets(unsigned char *ptr, Node *node, Reader ***readers,
		   bool ***seen);
  void WriteRegrets(unsigned char *ptr, Node *node, Writer ***writers,
		    bool ***seen);
  void ReadSumprobs(unsigned char *ptr, Node *node, Reader ***readers,
		    bool ***seen);
  void WriteSumprobs(unsigned char *ptr, Node *node, Writer ***writers,
		     bool ***seen);
  void Read(unsigned int batch_base);
  void Write(unsigned int batch_base);
  void Run(void);
  void RunBatch(unsigned int batch_size);
  unsigned char *Prepare(unsigned char *ptr, Node *node,
			 unsigned short last_bet_to,
			 unsigned long long int ***offsets);
  void MeasureTree(Node *node, bool ***seen,
		   unsigned long long int *allocation_size);
  void Prepare(void);
  // In extract_cvs
  void Walk(Node *node, unsigned char *ptr, string *action_sequences,
	    unsigned int it);

  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  unique_ptr<BettingTree> betting_tree_;
  bool asymmetric_;
  bool boost_;
  bool maintain_cvs_;
  unsigned int num_players_;
  unsigned int target_player_;
  unsigned char *data_;
  unsigned int batch_base_;
  unsigned int num_cfr_threads_;
  TCFRThread **cfr_threads_;
  float *rngs_;
  unsigned int *uncompress_;
  unsigned int *short_uncompress_;
  unsigned int max_street_;
  unsigned int *pruning_thresholds_;
  bool **sumprob_streets_;
  unique_ptr<bool []> char_quantized_streets_;
  unique_ptr<bool []> short_quantized_streets_;
  unsigned char *hvb_table_;
  unsigned char ***cards_to_indices_;
  unsigned int num_raw_boards_;
  unique_ptr<unsigned int []> board_table_;
  unsigned long long int total_process_count_;
  unsigned long long int total_full_process_count_;
  unsigned long long int total_its_;
};

#endif
