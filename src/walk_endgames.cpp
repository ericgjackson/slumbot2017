#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "eg_cfr.h"
#include "endgame_walker.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card params> "
	  "<endgame card params> <base betting params> "
	  "<endgame betting params> <base CFR params> <endgame CFR params> "
	  "<solve street> <base it> <num endgame its> "
	  "[unsafe|cfrd|maxmargin|combined] [cbrs|cfrs] [card|bucket] "
	  "[zerosum|raw]\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 15) Usage(argv[0]);
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
  unique_ptr<BettingAbstraction>endgame_betting_abstraction(
		   new BettingAbstraction(*endgame_betting_params));
  unique_ptr<Params> base_cfr_params = CreateCFRParams();
  base_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig> base_cfr_config(new CFRConfig(*base_cfr_params));
  unique_ptr<Params> endgame_cfr_params = CreateCFRParams();
  endgame_cfr_params->ReadFromFile(argv[7]);
  unique_ptr<CFRConfig> endgame_cfr_config(new CFRConfig(*endgame_cfr_params));
  unsigned int solve_street, base_it, num_endgame_its;
  if (sscanf(argv[8], "%u", &solve_street) != 1)     Usage(argv[0]);
  if (sscanf(argv[9], "%u", &base_it) != 1)         Usage(argv[0]);
  if (sscanf(argv[10], "%u", &num_endgame_its) != 1) Usage(argv[0]);
  string m = argv[11];
  ResolvingMethod method;
  if (m == "unsafe")         method = ResolvingMethod::UNSAFE;
  else if (m == "cfrd")      method = ResolvingMethod::CFRD;
  else if (m == "maxmargin") method = ResolvingMethod::MAXMARGIN;
  else if (m == "combined")  method = ResolvingMethod::COMBINED;
  else                       Usage(argv[0]);
  string v = argv[12];
  bool cfrs;
  if (v == "cbrs")      cfrs = false;
  else if (v == "cfrs") cfrs = true;
  else                  Usage(argv[0]);
  string l = argv[13];
  bool card_level;
  if (l == "card")        card_level = true;
  else if (l == "bucket") card_level = false;
  else                    Usage(argv[0]);
  string z = argv[14];
  bool zero_sum;
  if (z == "zerosum")  zero_sum = true;
  else if (z == "raw") zero_sum = false;
  else                 Usage(argv[0]);
  // runtime_config->SetIteration(base_it);
  Buckets base_buckets(*base_card_abstraction, false);
  Buckets endgame_buckets(*endgame_card_abstraction, false);

  BoardTree::Create();
  BettingTree *base_betting_tree =
    BettingTree::BuildTree(*base_betting_abstraction);
  BettingTree *endgame_betting_tree =
    BettingTree::BuildTree(*endgame_betting_abstraction);

  unsigned int num_threads = 1;
  EndgameWalker walker(solve_street, base_it, *base_card_abstraction,
		       *endgame_card_abstraction, *base_betting_abstraction,
		       *endgame_betting_abstraction, *base_cfr_config,
		       *endgame_cfr_config, base_buckets, endgame_buckets,
		       base_betting_tree, endgame_betting_tree,
		       method, cfrs, zero_sum, num_endgame_its, num_threads);
  walker.Go();

#if 0
  // For now
  if (method == ResolvingMethod::UNSAFE) {
    double p0_br, p1_br;
    solver.BRGo(&p0_br, &p1_br);
    fprintf(stderr, "Overall P0 BR: %f\n", p0_br);
    fprintf(stderr, "Overall P1 BR: %f\n", p1_br);
    double gap = p0_br + p1_br;
    fprintf(stderr, "Overall Gap: %f\n", gap);
    fprintf(stderr, "Overall Exploitability: %f mbb/g\n",
	    ((gap / 2.0) / 2.0) * 1000.0);
  }
#endif

  delete base_betting_tree;
  delete endgame_betting_tree;
}
