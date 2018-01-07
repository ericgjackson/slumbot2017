#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values_file.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"
#include "rand.h"

static void ShowProbs(CFRValuesFile *cvf, Node *node) {
  unsigned int pa = node->PlayerActing();
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int dsi = node->DefaultSuccIndex();
  unsigned int num_succs = node->NumSuccs();
  unique_ptr<double []> probs(new double[num_succs]);
  for (unsigned int b = 0; b < 3; ++b) {
    unsigned int offset = b * num_succs;
    cvf->Probs(pa, st, nt, offset, num_succs, dsi, probs.get());
    for (unsigned int s = 0; s < num_succs; ++s) {
      printf("b %u s %u prob %f\n", b, s, probs[s]);
    }
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card abstraction params> "
	  "<betting abstraction params> <CFR params> <it> <player>\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 7) {
    Usage(argv[0]);
  }

  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_params = CreateCardAbstractionParams();
  card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    card_abstraction(new CardAbstraction(*card_params));
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[3]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unique_ptr<Params> cfr_params = CreateCFRParams();
  cfr_params->ReadFromFile(argv[4]);
  unique_ptr<CFRConfig> cfr_config(new CFRConfig(*cfr_params));
  unsigned int it;
  if (sscanf(argv[5], "%u", &it) != 1) Usage(argv[0]);
  unsigned int p;
  if (sscanf(argv[6] + 1, "%u", &p) != 1) Usage(argv[0]);

  BettingTree *betting_tree;
  if (betting_abstraction->Asymmetric()) {
    betting_tree = BettingTree::BuildAsymmetricTree(*betting_abstraction, p);
  } else {
    betting_tree = BettingTree::BuildTree(*betting_abstraction);
  }

  Buckets buckets(*card_abstraction, true);
  CFRValuesFile cvf(nullptr, nullptr, *card_abstraction, *betting_abstraction,
		    *cfr_config, it, betting_tree, buckets.NumBuckets());
  Node *root = betting_tree->Root();
  ShowProbs(&cvf, root);
  Node *c = root->IthSucc(0);
  ShowProbs(&cvf, c);
  Node *cc = c->IthSucc(0);
  ShowProbs(&cvf, cc);
}
