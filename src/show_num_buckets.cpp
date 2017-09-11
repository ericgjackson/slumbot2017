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

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 3) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_abstraction_params = CreateCardAbstractionParams();
  card_abstraction_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction> ca(new CardAbstraction(*card_abstraction_params));
  Buckets buckets(*ca, true);
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    printf("St %u num buckets %u\n", st, buckets.NumBuckets(st));
  }
}
