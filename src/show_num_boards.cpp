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
  fprintf(stderr, "USAGE: %s <game params>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 2) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  BoardTree::Create();
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 1; st <= max_street; ++st) {
    printf("St %u num boards %u\n", st, BoardTree::NumBoards(st));
  }
}
