#ifndef _ECFR_H_
#define _ECFR_H_

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;
class ECFRThread;

class ECFR {
public:
  ECFR(const CardAbstraction &ca, const BettingAbstraction &ba,
       const CFRConfig &cc, const Buckets &buckets,
       unsigned int num_threads);
  ~ECFR(void);
  void Run(unsigned int start_batch_base, unsigned int end_batch_base,
	   unsigned int batch_size, unsigned int save_interval);
private:
  void Run(void);
  void RunBatch(unsigned int batch_size);
  void ReadRegrets(Node *node, Reader ***regret_readers);
  void ReadSumprobs(Node *node, Reader ***sumprob_readers);
  void ReadActionSumprobs(Node *node, Reader ***readers);
  void Read(unsigned int batch_base);
  void WriteRegrets(Node *node, Writer ***writers);
  void WriteSumprobs(Node *node, Writer ***writers);
  void WriteActionSumprobs(Node *node, Writer ***writers);
  void Write(unsigned int batch_base);
  void Initialize(Node *node);

  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  unique_ptr<BettingTree> betting_tree_;
  double ****regrets_;
  double ****sumprobs_;
  double ****action_sumprobs_;
  unsigned int num_raw_boards_;
  unique_ptr<unsigned int []> board_table_;
  unsigned int **bucket_counts_;
  unsigned int num_cfr_threads_;
  ECFRThread **cfr_threads_;
  unsigned int batch_base_;
  unsigned long long int total_its_;
};

#endif
