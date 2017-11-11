#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"
#include "rand.h"
#include "sampled_bcfr_builder.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> [p0|p1|all] <it> <sample street> <num to sample> "
	  "<num threads> [determ|nondeterm]\n", prog_name);
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

  unsigned int p = kMaxUInt;
  bool all = false;
  string p_arg = argv[5];
  if (p_arg == "p0")       p = 0;
  else if (p_arg == "p1")  p = 1;
  else if (p_arg == "all") all = true;
  else                     Usage(argv[0]);
  unsigned int it, sample_st, num_to_sample, num_threads;
  if (sscanf(argv[6], "%u", &it) != 1)            Usage(argv[0]);
  if (sscanf(argv[7], "%u", &sample_st) != 1)     Usage(argv[0]);
  if (sscanf(argv[8], "%u", &num_to_sample) != 1) Usage(argv[0]);
  if (sscanf(argv[9], "%u", &num_threads) != 1)   Usage(argv[0]);
  // We don't get deterministic results with multiple threads
  bool determ;
  string darg = argv[10];
  if (darg == "determ")         determ = true;
  else if (darg == "nondeterm") determ = false;
  else                          Usage(argv[0]);

  Buckets buckets(*card_abstraction, false);

  if (determ) {
    SeedRand(0);
  } else {
    InitRand();
  }

  unique_ptr<BettingTree>
    betting_tree(BettingTree::BuildTree(*betting_abstraction));

  unsigned int num_players = Game::NumPlayers();
  for (unsigned int pl = 0; pl < num_players; ++pl) {
    if (all || p == pl) {
      fprintf(stderr, "P%u\n", pl);
      SampledBCFRBuilder builder(*card_abstraction, *betting_abstraction,
				 *cfr_config, buckets, betting_tree.get(), pl,
				 it, sample_st, num_to_sample, num_threads);
      builder.Go();
    }
  }
}
