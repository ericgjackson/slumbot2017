#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "board_tree.h"
#include "buckets.h"
#include "cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"

using namespace std;

static void Show(const Buckets &buckets, unsigned int street) {
  BoardTree::Create();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  unsigned int max_card = Game::MaxCard();
  unsigned int num_board_cards = Game::NumBoardCards(street);
  unsigned int num_boards = BoardTree::NumBoards(street);

  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    const Card *board = BoardTree::Board(street, bd);
    if (num_hole_cards == 1) {
    } else {
      unsigned int hcp = 0;
      for (unsigned int hi = 1; hi <= max_card; ++hi) {
	if (InCards(hi, board, num_board_cards)) continue;
	for (unsigned int lo = 0; lo < hi; ++lo) {
	  if (InCards(lo, board, num_board_cards)) continue;
	  unsigned int h = bd * num_hole_card_pairs + hcp;
	  unsigned int b = buckets.Bucket(street, h);
	  printf("%u: ", b);
	  OutputNCards(board, num_board_cards);
	  printf(" / ");
	  OutputTwoCards(hi, lo);
	  printf(" (bd %u h %u)\n", bd, h);
	  fflush(stdout);
	  ++hcp;
	}
      }
    }
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <street>\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 4) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_abstraction_params = CreateCardAbstractionParams();
  card_abstraction_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction> ca(new CardAbstraction(*card_abstraction_params));
  unsigned int street;
  if (sscanf(argv[3], "%u", &street) != 1) Usage(argv[0]);
  Buckets buckets(*ca, false);
  Show(buckets, street);
}
