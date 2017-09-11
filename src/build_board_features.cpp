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

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <street> <features name> "
	  "<top n>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 5) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unsigned int street;
  if (sscanf(argv[2], "%u", &street) != 1) Usage(argv[0]);
  string features_name = argv[3];
  unsigned int top_n;
  if (sscanf(argv[4], "%u", &top_n) != 1)  Usage(argv[0]);

  BoardTree::Create();


  char buf[500];
  sprintf(buf, "%s/features.%s.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), features_name.c_str(),
	  street);
  Writer writer(buf);
  writer.WriteUnsignedInt(top_n);

  unsigned int num_boards = BoardTree::NumBoards(street);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  unsigned int num_hands = num_boards * num_hole_card_pairs;
  fprintf(stderr, "%u hands\n", num_hands);
  unsigned int num_board_cards = Game::NumBoardCards(street);
  vector<unsigned int> ranks(num_board_cards);
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    const Card *board = BoardTree::Board(street, bd);
    for (unsigned i = 0; i < num_board_cards; ++i) {
      ranks[i] = Rank(board[i]);
    }
    sort(ranks.begin(), ranks.end());
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      for (int j = top_n - 1; j >= 0; --j) {
	writer.WriteShort(ranks[j]);
      }
    }
#if 0
    unsigned int max_card = Game::MaxCard();
    for (Card hi = 1; hi <= max_card; ++hi) {
      if (InCards(hi, board, num_board_cards)) continue;
      for (Card lo = 0; lo < hi; ++lo) {
	if (InCards(lo, board, num_board_cards)) continue;
      }
    }
#endif
  }
}
