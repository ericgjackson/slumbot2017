#ifndef _CFRP_H_
#define _CFRP_H_

#include <string>
#include <vector>

#include "vcfr.h"

using namespace std;

class BettingTree;
class Buckets;
class CanonicalCards;
class CFRConfig;
class HandTree;
class Node;
class Reader;
class Writer;

class CFRP : public VCFR {
public:
  CFRP(const CardAbstraction &ca, const BettingAbstraction &ba,
       const CFRConfig &cc, const Buckets &buckets,
       const BettingTree *betting_tree, unsigned int num_threads,
       unsigned int target_p);
  virtual ~CFRP(void);
  void Run(unsigned int start_it, unsigned int end_it);
 protected:
  void FloorRegrets(Node *node, unsigned int p);
  void HalfIteration(unsigned int p);
  void Checkpoint(unsigned int it);
  void ReadFromCheckpoint(unsigned int it);

  const HandTree *hand_tree_;
  unique_ptr<CFRValues> regrets_;
  unique_ptr<CFRValues> sumprobs_;
};

#endif
