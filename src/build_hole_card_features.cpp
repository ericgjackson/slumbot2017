#include <stdio.h>
#include <stdlib.h>

#include "board_tree.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <street>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 3) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unsigned int st;
  if (sscanf(argv[2], "%u", &st) != 1) Usage(argv[0]);

  BoardTree::Create();
  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int num_board_cards = Game::NumBoardCards(st);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  fprintf(stderr, "%u hands\n", num_boards * num_hole_card_pairs);

  char buf[500];
  sprintf(buf, "%s/features.%s.%u.holecard.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), st);
  Writer writer(buf);
  writer.WriteUnsignedInt(1);
  Card max_card = Game::MaxCard();
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    const Card *board = BoardTree::Board(st, bd);
    Card hi, lo;
    for (hi = 1; hi <= max_card; ++hi) {
      if (InCards(hi, board, num_board_cards)) continue;
      unsigned int hr = Rank(hi);
      for (lo = 0; lo < hi; ++lo) {
	if (InCards(lo, board, num_board_cards)) continue;
	unsigned int lr = Rank(lo);
	short rank_code = (hr + 1) * hr / 2 + lr;
	writer.WriteShort(rank_code);
      }
    }
  }
}
