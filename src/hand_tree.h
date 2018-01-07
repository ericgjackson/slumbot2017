#ifndef _HAND_TREE_H_
#define _HAND_TREE_H_

#include "cards.h"

class CanonicalCards;

class HandTree {
public:
  HandTree(unsigned int root_st, unsigned int root_bd, unsigned int final_st);
  ~HandTree(void);
  const CanonicalCards *Hands(unsigned int st, unsigned int lbd) const {
    return hands_[st][lbd];
  }
  unsigned int RootSt(void) const {return root_st_;}
private:
  unsigned int root_st_;
  unsigned int root_bd_;
  unsigned int final_st_;
  CanonicalCards ***hands_;
};

unsigned int HCPIndex(unsigned int st, const Card *cards);

#endif
