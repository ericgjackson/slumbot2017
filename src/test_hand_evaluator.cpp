#include <stdio.h>
#include <stdlib.h>

#include "cards.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_evaluator.h"
#include "hand_value_tree.h"
#include "params.h"

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE %s <game params>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 2) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  HandValueTree::Create();
  HoldemHandEvaluator evaluator;
  Card cards[3];
  cards[0] = MakeCard(10, 0);
  cards[1] = MakeCard(12, 3);
  cards[2] = MakeCard(11, 2);
  int hv1 = evaluator.Evaluate(cards, 3);
  unsigned int thv1 = HandValueTree::Val(cards);
  cards[0] = MakeCard(9, 0);
  int hv2 = evaluator.Evaluate(cards, 3);
  unsigned int thv2 = HandValueTree::Val(cards);
  printf("hv1 %i hv2 %i\n", hv1, hv2);
  printf("thv1 %u thv2 %u\n", thv1, thv2);
}
