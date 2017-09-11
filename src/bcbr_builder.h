#ifndef _BCBR_BUILDER_H_
#define _BCBR_BUILDER_H_

#include <memory>

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class BCBRThread;
class BCFRThread;
class CFRConfig;
class HandTree;

class BCBRBuilder {
public:
  BCBRBuilder(const CardAbstraction &ca, const BettingAbstraction &ba,
	      const CFRConfig &cc, const Buckets &buckets,
	      bool cfrs, unsigned int p, unsigned int it,
	      unsigned int num_threads);
  ~BCBRBuilder(void);
  void Go(void);
private:
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  unsigned int num_threads_;
  const BettingTree *betting_tree_;
  HandTree *trunk_hand_tree_;
  BCBRThread *trunk_cbr_thread_;
  BCFRThread *trunk_cfr_thread_;
  BCBRThread **threads_;
};

#endif
