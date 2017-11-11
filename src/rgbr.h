#ifndef _RGBR_H_
#define _RGBR_H_

#include "vcfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;

class RGBR : public VCFR {
public:
  RGBR(const CardAbstraction &ca, const BettingAbstraction &ba,
       const CFRConfig &cc, const Buckets &buckets,
       const BettingTree *betting_tree, bool current, unsigned int num_threads,
       const bool *streets, bool always_call_preflop);
  virtual ~RGBR(void);
  double Go(unsigned int it, unsigned int p);

 protected:
  const HandTree *hand_tree_;
};

#endif
