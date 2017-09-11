#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cbr_builder.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> [p0|p1|both] <it> <num threads>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 8) Usage(argv[0]);
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

  bool p0 = false, p1 = false, both = false;
  string p_arg = argv[5];
  if (p_arg == "p0")        p0 = true;
  else if (p_arg == "p1")   p1 = true;
  else if (p_arg == "both") both = true;
  else                      Usage(argv[0]);
  unsigned int it;
  if (sscanf(argv[6], "%u", &it) != 1)        Usage(argv[0]);
  unsigned int num_threads;
  if (sscanf(argv[7], "%u", &num_threads) != 1) Usage(argv[0]);
  Buckets buckets(*card_abstraction, false);
  
  if (both || p0) {
    fprintf(stderr, "P0\n");
    CBRBuilder builder(*card_abstraction, *betting_abstraction,
		       *cfr_config, buckets, true, 0, it, num_threads);
    builder.Go();
  }
  if (both || p1) {
    fprintf(stderr, "P1\n");
    CBRBuilder builder(*card_abstraction, *betting_abstraction,
		       *cfr_config, buckets, true, 1, it, num_threads);
    builder.Go();
  }
}
