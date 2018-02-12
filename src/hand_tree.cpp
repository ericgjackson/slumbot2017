// Maintains all the hands rooted at a given street and a given board.
// For small games you can maintain all possible hands by rooting at the
// preflop.  For large games, you might create the HandTree for all hands
// rooted at a particular flop board.

#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "board_tree.h"
#include "canonical_cards.h"
#include "cards.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"

using namespace std;

HandTree::HandTree(unsigned int root_st, unsigned int root_bd,
		   unsigned int final_st) {
  root_st_ = root_st;
  root_bd_ = root_bd;
  final_st_ = final_st;
  hands_ = new CanonicalCards **[final_st_ + 1];
  for (unsigned int st = 0; st < root_st_; ++st) {
    hands_[st] = NULL;
  }
  BoardTree::Create();
  unsigned int max_street = Game::MaxStreet();
  if (final_st == max_street) {
    // Used to lazily instantiate, but that doesn't currently work in
    // multi-threaded environment because HandValueTree::Create() is not
    // threadsafe.  (Should just fix that.)
    if (! HandValueTree::Created()) {
      fprintf(stderr, "Hand value tree has not been created\n");
      exit(-1);
    }
  }
  for (unsigned int st = root_st_; st <= final_st_; ++st) {
    unsigned int num_local_boards =
      BoardTree::NumLocalBoards(root_st_, root_bd_, st);
    unsigned int num_board_cards = Game::NumBoardCards(st);
    hands_[st] = new CanonicalCards *[num_local_boards];
    for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
      unsigned int gbd = BoardTree::GlobalIndex(root_st_, root_bd_, st, lbd);
      const Card *board = BoardTree::Board(st, gbd);
      unsigned int sg = BoardTree::SuitGroups(st, gbd);
      hands_[st][lbd] = new CanonicalCards(2, board, num_board_cards, sg,
					   false);
      if (st == max_street) {
	hands_[st][lbd]->SortByHandStrength(board);
      }
    }
  }
}

HandTree::~HandTree(void) {
  for (unsigned int st = root_st_; st <= final_st_; ++st) {
    unsigned int num_local_boards =
      BoardTree::NumLocalBoards(root_st_, root_bd_, st);
    for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
      delete hands_[st][lbd];
    }
    delete [] hands_[st];
  }
  delete [] hands_;
}

// Assumes hole cards are ordered
unsigned int HCPIndex(unsigned int st, const Card *cards) {
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  const Card *board = cards + num_hole_cards;
  unsigned int num_board_cards = Game::NumBoardCards(st);
  if (num_hole_cards == 1) {
    unsigned int c = cards[0];
    unsigned int num_board_lower = 0;
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      if ((unsigned int)board[i] < c) ++num_board_lower;
    }
    return c - num_board_lower;
  } else {
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int num_lower_lo = 0, num_lower_hi = 0;
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      Card c = board[i];
      if (c < lo) {
	++num_lower_lo;
	++num_lower_hi;
      } else if (c < hi) {
	++num_lower_hi;
      }
    }
    unsigned int hi_index = hi - num_lower_hi;
    unsigned int lo_index = lo - num_lower_lo;
    // The sum from 1... hi_index - 1 is the number of hole card pairs
    // containing a high card less than hi.
    return (hi_index - 1) * hi_index / 2 + lo_index;
  }
}
