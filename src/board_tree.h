#ifndef _BOARD_TREE_H_
#define _BOARD_TREE_H_

#include <memory>

#include "cards.h"
#include "game.h"

using namespace std;

class BoardTree {
public:
  static void Create(void);
  static void Delete(void);
  static const Card *Board(unsigned int st, unsigned int bd) {
    if (st == 0) return NULL;
    unsigned int num_board_cards = Game::NumBoardCards(st);
    return &boards_[st][bd * num_board_cards];
  }
  static unsigned int SuitGroups(unsigned int st, unsigned int bd) {
    return suit_groups_[st][bd];
  }
  // The number of isomorphic variants of the current street addition to the
  // board.  For example, for AcKcQc9d the number of variants is 3 because the
  // 9d turns has three isomorphic variants (9d, 9h, 9s).
  static unsigned int NumVariants(unsigned int st, unsigned int bd) {
    return board_variants_[st][bd];
  }
  static unsigned int LocalIndex(unsigned int root_st, unsigned int root_bd,
				 unsigned int st, unsigned int gbd);
  static unsigned int GlobalIndex(unsigned int root_st, unsigned int root_bd,
				  unsigned int st, unsigned int lbd);
  static unsigned int NumLocalBoards(unsigned int root_st, unsigned int root_bd,
				     unsigned int st);
  static unsigned int NumBoards(unsigned int st) {return num_boards_[st];}
  static unsigned int SuccBoardBegin(unsigned int root_st,
				     unsigned int root_bd, unsigned int st) {
    return succ_board_begins_[root_st][st][root_bd];
  }
  static unsigned int SuccBoardEnd(unsigned int root_st,
				   unsigned int root_bd, unsigned int st) {
    return succ_board_ends_[root_st][st][root_bd];
  }
  static void CreateLookup(void);
  static void DeleteLookup(void);
  static unsigned int LookupBoard(const Card *board, unsigned int st);
  static void BuildBoardCounts(void);
  static void DeleteBoardCounts(void);
  static unsigned int BoardCount(unsigned int st, unsigned int bd) {
    return board_counts_[st][bd];
  }
  static void BuildPredBoards(void);
  static void DeletePredBoards(void);
  static unsigned int PredBoard(unsigned int msbd, unsigned int pst) {
    return pred_boards_[msbd * max_street_ + pst];
  }
private:
  BoardTree(void) {}
  
  static void Count(unsigned int st, const Card *prev_board,
		    unsigned int prev_sg);
  static void Build(unsigned int st, const unique_ptr<Card []> &prev_board,
		    unsigned int prev_sg);
  static void DealRawBoards(Card *board, unsigned int st);
  static void BuildPredBoards(unsigned int st, unsigned int *pred_bds);

  static unsigned int max_street_;
  static unique_ptr<unsigned int []> num_boards_;
  static unsigned int **board_variants_;
  static unsigned int ***succ_board_begins_;
  static unsigned int ***succ_board_ends_;
  static Card **boards_;
  static unsigned int **suit_groups_;
  static unsigned int *bds_;
  static unsigned int **lookup_;
  static unsigned int **board_counts_;
  static unsigned int *pred_boards_;
};

#endif
