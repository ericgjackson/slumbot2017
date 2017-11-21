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
  fprintf(stderr, "USAGE: %s <game params> <street> <features>\n", prog_name);
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
  string features = argv[3];

  // Just need this to get number of hands
  BoardTree::Create();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  unsigned int num_hands = BoardTree::NumBoards(street) * num_hole_card_pairs;
  fprintf(stderr, "%u hands\n", num_hands);

  char buf[500];
  sprintf(buf, "%s/features.%s.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), features.c_str(),
	  street);
  Reader reader(buf);
  unsigned int num_features = reader.ReadUnsignedIntOrDie();
  fprintf(stderr, "%u features\n", num_features);
  for (unsigned int h = 0; h < num_hands; ++h) {
    printf("h %u", h);
    for (unsigned int f = 0; f < num_features; ++f) {
      short fv = reader.ReadShortOrDie();
      printf(" %i", (int)fv);
    }
    printf("\n");
    fflush(stdout);
  }
}
