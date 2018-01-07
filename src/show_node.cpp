// Tree constructor requires card abstraction because nonterminal IDs are
// indexed by granularity, and the card abstraction gives the number of
// bucketings.

#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <betting params> "
	  "<st> <pa> <nt> ([p0|p1])\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 6 && argc != 7) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[2]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unsigned int st, pa, nt;
  if (sscanf(argv[3], "%u", &st) != 1) Usage(argv[0]);
  if (sscanf(argv[4], "%u", &pa) != 1) Usage(argv[0]);
  if (sscanf(argv[5], "%u", &nt) != 1) Usage(argv[0]);

  BettingTree *betting_tree = NULL;
  if (argc == 7) {
    string p_arg = argv[6];
    unsigned int p;
    if (p_arg == "p0")      p = 0;
    else if (p_arg == "p1") p = 1;
    else                    Usage(argv[0]);
    betting_tree = BettingTree::BuildAsymmetricTree(*betting_abstraction, p);
  } else {
    betting_tree = BettingTree::BuildTree(*betting_abstraction);
  }
  
  if (! betting_tree->PrintNode(st, pa, nt)) {
    fprintf(stderr, "Couldn't find node\n");
  }
}

