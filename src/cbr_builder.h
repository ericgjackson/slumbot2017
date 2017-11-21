#ifndef _CBR_BUILDER_H_
#define _CBR_BUILDER_H_

#include <memory>

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CBRThread;
class CFRConfig;
class HandTree;

class CBRBuilder {
public:
  CBRBuilder(const CardAbstraction &ca, const BettingAbstraction &ba,
	     const CFRConfig &cc, const Buckets &buckets, bool cfrs,
	     unsigned int p, unsigned int it, unsigned int num_threads);
  ~CBRBuilder(void);
  void Go(void);
private:
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  unsigned int num_threads_;
  const BettingTree *betting_tree_;
  HandTree *trunk_hand_tree_;
  CBRThread *trunk_thread_;
};

#endif
