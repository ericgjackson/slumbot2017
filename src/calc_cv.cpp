#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cv_calc_thread.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "params.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <st> <pa> <nt> <bd> <num threads>\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 11) Usage(argv[0]);
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
  unsigned int it, st, pa, nt, bd, num_threads;
  if (sscanf(argv[5], "%u", &it) != 1)           Usage(argv[0]);
  if (sscanf(argv[6], "%u", &st) != 1)           Usage(argv[0]);
  if (sscanf(argv[7], "%u", &pa) != 1)           Usage(argv[0]);
  if (sscanf(argv[8], "%u", &nt) != 1)           Usage(argv[0]);
  if (sscanf(argv[9], "%u", &bd) != 1)           Usage(argv[0]);
  if (sscanf(argv[10], "%u", &num_threads) != 1) Usage(argv[0]);
  
  Buckets buckets(*card_abstraction, false);
  unique_ptr<BettingTree> betting_tree;
  betting_tree.reset(BettingTree::BuildTree(*betting_abstraction));
  Node *node = betting_tree->FindNode(st, pa, nt);
  if (node == nullptr) {
    fprintf(stderr, "Couldn't find node\n");
    exit(-1);
  }
  
  HandValueTree::Create();

  CVCalcThread t(*card_abstraction, *betting_abstraction, *cfr_config,
		 buckets, betting_tree.get(), num_threads, it);
  
  t.Go(node, bd);
}
