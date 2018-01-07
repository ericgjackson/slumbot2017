#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "bot.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "nl_agent.h"
#include "params.h"
#include "rand.h"
#include "runtime_config.h"
#include "runtime_params.h"

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card abstraction params> "
	  "<endgame card abstraction params> "
	  "<base betting abstraction params> "
	  "<endgame betting abstraction params> <base CFR params> "
	  "<endgame CFR params> <runtime params> "
	  "<endgame st> <num endgame its> <its> (optional args) <server> "
	  "<port>\n", prog_name);
  fprintf(stderr, "Optional arguments:\n");
  fprintf(stderr, "  debug: generate debugging output\n");
  fprintf(stderr, "  eoe: exit on error\n");
  fprintf(stderr, "  fs: fixed seed\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc < 14) {
    Usage(argv[0]);
  }

  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> base_card_params = CreateCardAbstractionParams();
  base_card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    base_card_abstraction(new CardAbstraction(*base_card_params));
  unique_ptr<Params> endgame_card_params = CreateCardAbstractionParams();
  endgame_card_params->ReadFromFile(argv[3]);
  unique_ptr<CardAbstraction>
    endgame_card_abstraction(new CardAbstraction(*endgame_card_params));
  unique_ptr<Params> base_betting_params = CreateBettingAbstractionParams();
  base_betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    base_betting_abstraction(new BettingAbstraction(*base_betting_params));
  unique_ptr<Params> endgame_betting_params = CreateBettingAbstractionParams();
  endgame_betting_params->ReadFromFile(argv[5]);
  unique_ptr<BettingAbstraction>
    endgame_betting_abstraction(
			  new BettingAbstraction(*endgame_betting_params));
  unique_ptr<Params> base_cfr_params = CreateCFRParams();
  base_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig> base_cfr_config(new CFRConfig(*base_cfr_params));
  unique_ptr<Params> endgame_cfr_params = CreateCFRParams();
  endgame_cfr_params->ReadFromFile(argv[7]);
  unique_ptr<CFRConfig> endgame_cfr_config(new CFRConfig(*endgame_cfr_params));

  unique_ptr<Params> runtime_params = CreateRuntimeParams();
  runtime_params->ReadFromFile(argv[8]);
  unique_ptr<RuntimeConfig>
    runtime_config(new RuntimeConfig(*runtime_params));
  unsigned int endgame_st, num_endgame_its;
  if (sscanf(argv[9], "%u", &endgame_st) != 1) Usage(argv[0]);
  if (sscanf(argv[10], "%u", &num_endgame_its) != 1) Usage(argv[0]);
  unsigned int num_players = Game::NumPlayers();
  unsigned int *iterations = new unsigned int[num_players];
  unsigned int a = 11;
  if (base_betting_abstraction->Asymmetric()) {
    unsigned int it;
    for (unsigned int p = 0; p < num_players; ++p) {
      if ((int)a >= argc) Usage(argv[0]);
      if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
      iterations[p] = it;
    }
  } else {
    unsigned int it;
    if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
    for (unsigned int p = 0; p < num_players; ++p) {
      iterations[p] = it;
    }
  }

  bool debug = false;             // Disabled by default
  bool exit_on_error = false;     // Disabled by default
  bool fixed_seed = false;
  for (int i = a; i < argc - 2; ++i) {
    string arg = argv[i];
    if (arg == "debug") {
      debug = true;
    } else if (arg == "eoe") {
      exit_on_error = true;
    } else if (arg == "fs") {
      fixed_seed = true;
    } else {
      Usage(argv[0]);
    }
  }

  const char *server = argv[argc - 2];
  int port;
  if (sscanf(argv[argc - 1], "%i", &port) != 1) {
    Usage(argv[0]);
  }

  InitRand();

  BettingTree **betting_trees = new BettingTree *[num_players];
  if (base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      betting_trees[p] =
	BettingTree::BuildAsymmetricTree(*base_betting_abstraction, p);
    }
  } else {
    BettingTree *betting_tree =
      BettingTree::BuildTree(*base_betting_abstraction);
    for (unsigned int p = 0; p < num_players; ++p) {
      betting_trees[p] = betting_tree;
    }
  }

  unsigned int small_blind = 50;
  unsigned int stack_size = 20000;
  NLAgent agent(*base_card_abstraction, *endgame_card_abstraction,
		*base_betting_abstraction, *endgame_betting_abstraction,
		*base_cfr_config, *endgame_cfr_config, *runtime_config,
		iterations, betting_trees, endgame_st, num_endgame_its, debug,
		exit_on_error, fixed_seed, small_blind, stack_size);

  Bot bot(&agent);
  bot.Connect(server, port);
  bot.Run();

  if (base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete betting_trees[p];
    }
  } else {
    delete betting_trees[0];
  }
  delete [] betting_trees;
}
