#ifndef _TCFR_H_
#define _TCFR_H_

#include "cfr.h"

using namespace std;

class BettingAbstraction;
class Buckets;
class CardAbstraction;
class CFRConfig;
class Node;
class Reader;
class TCFRThread;
class Writer;

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
private:
  void ReadRegrets(unsigned char *ptr, Reader ***readers);
  void WriteRegrets(unsigned char *ptr, Writer ***writers);
  void ReadSumprobs(unsigned char *ptr, Reader ***readers);
  void WriteSumprobs(unsigned char *ptr, Writer ***writers);
  void Read(unsigned int batch_base);
  void Write(unsigned int batch_base);
  void Run(void);
  void RunBatch(unsigned int batch_size);
  unsigned char *Prepare(unsigned char *ptr, Node *node);
  void MeasureTree(Node *node, unsigned long long int *allocation_size);
  void Prepare(void);

  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  bool asymmetric_;
  unsigned int target_player_;
  unsigned char *data_;
  unsigned int batch_base_;
  unsigned int num_cfr_threads_;
  TCFRThread **cfr_threads_;
  unsigned int *canonical_boards_;
  unsigned int num_raw_boards_;
  float *rngs_;
  unsigned int *uncompress_;
  unsigned int *short_uncompress_;
  unsigned int max_street_;
  unsigned int *pruning_thresholds_;
  bool *sumprob_streets_;
  bool *quantized_streets_;
  bool *short_quantized_streets_;
  unsigned char *hvb_table_;
  unsigned char ***cards_to_indices_;
  unsigned long long int total_process_count_;
  unsigned long long int total_full_process_count_;
};

#endif
