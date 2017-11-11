#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "betting_tree.h"

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"

void Walk(Node *node, const string &action_sequence) {
  if (node->Terminal()) return;

  if (action_sequence == "xcccb2b8b6c") {
    fprintf(stderr, "Found it nt %u\n", node->NonterminalID());
    exit(-1);
  }

  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Walk(node->IthSucc(s), action_sequence + action);
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <betting params>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 3) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[2]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));

  BettingTree *betting_tree = BettingTree::BuildTree(*betting_abstraction);
  Walk(betting_tree->Root(), "x");
}
