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

static void Verify(Node *node, unsigned int expected_pa,
		   unsigned int last_st) {
  unsigned int st = node->Street();
  if (st > last_st) {
    Verify(node, Game::FirstToAct(st), st);
    return;
  }
  unsigned int pa = node->PlayerActing();
  if (pa != expected_pa && ! node->Showdown()) {
    if (node->Terminal()) {
      fprintf(stderr, "Mismatch st %u tid %u\n", st, node->TerminalID());
    } else {
      fprintf(stderr, "Mismatch st %u pa %u ntid %u\n", st, pa,
	      node->NonterminalID());
    }
    exit(-1);
  }
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Verify(node->IthSucc(s), expected_pa^1, st);
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
  Verify(betting_tree->Root(), 1, 0);
}
