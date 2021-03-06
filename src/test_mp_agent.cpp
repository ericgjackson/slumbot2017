#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"
#include "rand.h"
#include "runtime_config.h"
#include "runtime_params.h"
#include "split.h"
#include "nl_agent.h"

// FROM 1 at 1515454871.768963 MATCHSTATE:0:114:cr2130cc:9c7s|||||:r2500^M
static void IllegalRaise2(Agent *agent) {
  printf("IllegalRaise2\n");
  printf("-------------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:114:cr2130cc:9c7s|||||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("97o initial action %i we_bet_to %i\n", ba, we_bet_to);
}

// MATCHSTATE:0:12:cr8246r16980c:4cAh|||||
// MATCHSTATE:0:12:cr8246r16980ccfr20000ccc:4cAh|||||
// P2 calls, P3 raises to 8246, P4 raises to 16980, P5 calls, P0 calls,
// P1 folds, P2 raises to 20000, P3 calls, P4 calls, P5 calls,
// MATCHSTATE:0:12:cr8246r16980ccfr20000cccc///:4cAh||Tc3h|4dKc|Ac3s|Qh8s/4hKd5d/Js/7d
static void AlreadyAllIn2(Agent *agent) {
  printf("AlreadyAllIn2\n");
  printf("-------------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:12:cr8246r16980c:4cAh|||||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A4o initial action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:0:12:cr8246r16980ccfr20000ccc:4cAh|||||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A4o second action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:0:12:cr8246r16980ccfr20000cccc///:4cAh||Tc3h|4dKc|Ac3s|Qh8s/4hKd5d/Js/7d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("A4o third action %i we_bet_to %i\n", ba, we_bet_to);
}

// FROM 1 at 1515451178.072253 MATCHSTATE:0:78:r3397r10458cr18247cr20000ccccc///:Jh7c|5hQs|5s9s|JsTh|Jc8d|Ah7d/2sAsTc/Qh/3d:c
// WARNING: ignoring un-requested response
static void AlreadyAllIn(Agent *agent) {
  printf("AlreadyAllIn\n");
  printf("------------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:78:r3397r10458cr18247:Jh7c|||||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("J7o initial action %i we_bet_to %i\n", ba, we_bet_to);
#if 0
  match_state = "MATCHSTATE:0:78:r3397r10458cr18247cr20000ccccc///:Jh7c|5hQs|5s9s|JsTh|Jc8d|Ah7d/2sAsTc/Qh/3d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("J7o action %i we_bet_to %i\n", ba, we_bet_to);
#endif
}

// FROM 1 at 1515448453.115852 MATCHSTATE:0:0:cr10389cc:5d5c|||||:r10489
// WARNING: raise of 10489 increased to 20000
static void IllegalRaise(Agent *agent) {
  printf("IllegalRaise\n");
  printf("------------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:0:0:cr10389cc:5d5c|||||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("55 r10489 action %i we_bet_to %i\n", ba, we_bet_to);
}

static void PreflopP2(Agent *agent) {
  printf("PreflopP2\n");
  printf("---------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:2:1::||AsJc|||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo root action %i we_bet_to %i\n", ba, we_bet_to);
}

static void PreflopP3(Agent *agent) {
  printf("PreflopP3\n");
  printf("---------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:3:2:f:|||AsJc||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo f action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:3:r300:|||AsKc||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AKo r300 action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:4:r300:|||AsAh||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AA r300 action %i we_bet_to %i\n", ba, we_bet_to);
}

static void FlopP3(Agent *agent) {
  printf("FlopP3\n");
  printf("------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:3:5:f:|||AsJc||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo f action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:5:fr250fffc/c:|||AsJc||/Ah7d2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo fr250fffc/c action %i we_bet_to %i\n", ba, we_bet_to);
}

static void TurnP3(Agent *agent) {
  printf("TurnP3\n");
  printf("------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:3:6:f:|||AsJc||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo f action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:6:fr200fffc/c:|||AsJc||/Ah7d2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo fr200fffc/c action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:6:fr200fffc/cr300c/c:|||AsJc||/Ah7d2c/3c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo fr200fffc/cr300c/c action %i we_bet_to %i\n", ba, we_bet_to);
}

static void RiverP3(Agent *agent) {
  printf("RiverP3\n");
  printf("-------\n");
  string match_state;
  unsigned int we_bet_to;
  BotAction ba;

  match_state = "MATCHSTATE:3:7:f:|||AsJc||";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo f action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:7:fr200fffc/c:|||AsJc||/Ah7d2c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo fr200fffc/c action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:7:fr200fffc/cr300c/c:|||AsJc||/Ah7d2c/3c";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo fr200fffc/cr300c/c action %i we_bet_to %i\n", ba, we_bet_to);

  match_state = "MATCHSTATE:3:7:fr200fffc/cr300c/cr600c/c:|||AsJc||/Ah7d2c/3c/4d";
  ba = agent->HandleStateChange(match_state, &we_bet_to);
  printf("AJo fr200fffc/cr300c/cr600c/c action %i we_bet_to %i\n", ba,
	 we_bet_to);
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card abstraction params> "
	  "<endgame card abstraction params> "
	  "<base betting abstraction params> "
	  "<endgame betting abstraction params> <base CFR params> "
	  "<endgame CFR params> <runtime params> "
	  "<num endgame its> <its> (optional args)\n", prog_name);
  fprintf(stderr, "Optional arguments:\n");
  fprintf(stderr, "  debug: generate debugging output\n");
  fprintf(stderr, "  eoe: exit on error\n");
  fprintf(stderr, "  fs: fixed seed\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc < 11) {
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
  unsigned int num_endgame_its;
  if (sscanf(argv[9], "%u", &num_endgame_its) != 1) Usage(argv[0]);
  unsigned int num_players = Game::NumPlayers();
  unsigned int *iterations = new unsigned int[num_players];
  unsigned int a = 10;
  if (base_betting_abstraction->Asymmetric()) {
    unsigned int it;
    for (unsigned int p = 0; p < num_players; ++p) {
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
  for (int i = a; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "debug") {
      debug = true;
    } else if (arg == "eoe") {
      exit_on_error = true;
    } else {
      Usage(argv[0]);
    }
  }

  InitRandFixed();

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
  bool fixed_seed = true;
  NLAgent agent(*base_card_abstraction, *endgame_card_abstraction,
		*base_betting_abstraction, *endgame_betting_abstraction,
		*base_cfr_config, *endgame_cfr_config, *runtime_config,
		iterations, betting_trees, 999, num_endgame_its, debug,
		exit_on_error, fixed_seed, small_blind, stack_size);

#if 0
  PreflopP2(&agent);
  PreflopP3(&agent);
  FlopP3(&agent);
  TurnP3(&agent);
  RiverP3(&agent);
  IllegalRaise(&agent);
  AlreadyAllIn(&agent);
  AlreadyAllIn2(&agent);
#endif
  IllegalRaise2(&agent);

  if (base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete betting_trees[p];
    }
  } else {
    delete betting_trees[0];
  }
  delete [] betting_trees;
}
