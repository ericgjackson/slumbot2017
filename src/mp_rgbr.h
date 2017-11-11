#ifndef _MP_RGBR_H_
#define _MP_RGBR_H_

#include "mp_vcfr.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;

class MPRGBR : public MPVCFR {
public:
  MPRGBR(const CardAbstraction &ca, const BettingAbstraction &ba,
	 const CFRConfig &cc, const Buckets &buckets,
	 const BettingTree *betting_tree, bool current,
	 unsigned int num_threads, const bool *streets);
  virtual ~MPRGBR(void);
  double Go(unsigned int it, unsigned int p);
};

#endif
