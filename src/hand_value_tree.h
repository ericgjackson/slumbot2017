#ifndef _HAND_VALUE_TREE_H_
#define _HAND_VALUE_TREE_H_

#include "cards.h"

class HandValueTree {
public:
  static void Create(void);
  static void Delete(void);
  // Does *not* assume cards are sorted
  static unsigned int Val(const Card *cards);
  // board and hole_cards should be sorted from high to low.
  static unsigned int Val(const unsigned int *board,
			  const unsigned int *hole_cards);
  static unsigned int DiskRead(Card *cards);
private:
  HandValueTree(void) {}

  static void ReadOne(void);
  static void ReadTwo(void);
  static void ReadThree(void);
  static void ReadFour(void);
  static void ReadFive(void);
  static void ReadSix(void);
  static void ReadSeven(void);

  static unsigned int num_board_cards_;
  static unsigned int num_cards_;
  static unsigned int *tree1_;
  static unsigned int **tree2_;
  static unsigned int ***tree3_;
  static unsigned int ****tree4_;
  static unsigned int *****tree5_;
  static unsigned int ******tree6_;
  static unsigned int *******tree7_;
};

#endif
