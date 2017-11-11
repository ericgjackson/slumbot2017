#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "board_tree.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"

using namespace std;

// Return 5 if five of a suit on the board
// Return 4 if four or more of a suit on the board
// Return 3 if three of a suit on the board
// Return 2 if two of a suit on the board by the flop
// Return 1 if two of a suit on the board by the turn
// return 0 otherwise
static unsigned int CategorizeBoardS4(const Card *board,
				      unsigned int num_board_cards) {
  if (num_board_cards != 5) exit(-1);
  unsigned int suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) suit_counts[s] = 0;
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    ++suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (suit_counts[s] >= 5) return 5;
    if (suit_counts[s] >= 4) return 4;
    if (suit_counts[s] == 3) return 3;
  }
  
  unsigned int flop_suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) flop_suit_counts[s] = 0;
  for (unsigned int i = 0; i < 3; ++i) {
    ++flop_suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (flop_suit_counts[s] == 2) return 2;
  }
  
  unsigned int turn_suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) turn_suit_counts[s] = 0;
  for (unsigned int i = 0; i < 4; ++i) {
    ++turn_suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (turn_suit_counts[s] == 2) return 1;
  }
  return 0;
}

// Return 4 if four or more of a suit on the board
// Return 3 if three of a suit on the board
// Return 2 if two of a suit on the board by the flop
// Return 1 if two of a suit on the board by the turn
// return 0 otherwise
static unsigned int CategorizeBoardS3(const Card *board,
				      unsigned int num_board_cards) {
  if (num_board_cards != 5) exit(-1);
  unsigned int suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) suit_counts[s] = 0;
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    ++suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (suit_counts[s] >= 4) return 4;
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (suit_counts[s] == 3) return 3;
  }
  
  unsigned int flop_suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) flop_suit_counts[s] = 0;
  for (unsigned int i = 0; i < 3; ++i) {
    ++flop_suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (flop_suit_counts[s] == 2) return 2;
  }
  
  unsigned int turn_suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) turn_suit_counts[s] = 0;
  for (unsigned int i = 0; i < 4; ++i) {
    ++turn_suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (turn_suit_counts[s] == 2) return 1;
  }
  return 0;
}

// Return 2 if four or more of a suit on the board
// Return 1 if three of a suit on the board
// Return 0 otherwise
static unsigned int CategorizeBoardS2(const Card *board,
				      unsigned int num_board_cards) {
  unsigned int suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) suit_counts[s] = 0;
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    ++suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (suit_counts[s] >= 4) return 2;
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (suit_counts[s] == 3) return 1;
  }
  return 0;
}

// Return 1 if three or more of a suit on the board
// Return 0 otherwise
static unsigned int CategorizeBoardS1(const Card *board,
				      unsigned int num_board_cards) {
  unsigned int suit_counts[4];
  for (unsigned int s = 0; s < 4; ++s) suit_counts[s] = 0;
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    ++suit_counts[Suit(board[i])];
  }
  for (unsigned int s = 0; s < 4; ++s) {
    if (suit_counts[s] >= 3) return 1;
  }
  return 0;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <street> <features name>\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 4) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unsigned int street;
  if (sscanf(argv[2], "%u", &street) != 1) Usage(argv[0]);
  string features_name = argv[3];

  if (features_name != "s1" && features_name != "s2" &&
      features_name != "s3" && features_name != "s4") {
    fprintf(stderr, "Unknown features name \"%s\"\n", features_name.c_str());
    exit(-1);
  }
  bool s1 = (features_name == "s1");
  bool s2 = (features_name == "s2");
  bool s3 = (features_name == "s3");
  bool s4 = (features_name == "s4");
  
  BoardTree::Create();

  char buf[500];
  sprintf(buf, "%s/features.%s.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), features_name.c_str(),
	  street);
  Writer writer(buf);
  writer.WriteUnsignedInt(1);

  unsigned int num_boards = BoardTree::NumBoards(street);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  unsigned int num_hands = num_boards * num_hole_card_pairs;
  fprintf(stderr, "%u hands\n", num_hands);
  unsigned int num_board_cards = Game::NumBoardCards(street);
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    const Card *board = BoardTree::Board(street, bd);
    short fv;
    if (s1) {
      fv = CategorizeBoardS1(board, num_board_cards);
    } else if (s2) {
      fv = CategorizeBoardS2(board, num_board_cards);
    } else if (s3) {
      fv = CategorizeBoardS3(board, num_board_cards);
    } else if (s4) {
      fv = CategorizeBoardS4(board, num_board_cards);
    } else {
      exit(-1);
    }
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      writer.WriteShort(fv);
    }
  }
}
