// I should only load probs and buckets for final street if I want to solve
// subtrees that are rooted *beyond* final street initial nodes.

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
#include "endgames.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "params.h"

using namespace std;

static void WalkTrunk2(Node *base_node, Node *endgame_node,
		       const string &action_sequence, EndgameSolver *solver,
		       BettingTree *endgame_betting_tree,
		       unsigned int target_st, unsigned int num_its,
		       Node *base_solve_root, Node *endgame_solve_root,
		       unsigned int solve_st, unsigned int solve_bd) {
  if (base_node->Terminal()) return;
  unsigned int st = base_node->Street();
  if (st == target_st) {
    // unsigned int p = base_node->PlayerActing();
    // unsigned int base_solve_nt = base_solve_root->NonterminalID();
    // unsigned int base_target_nt = base_node->NonterminalID();
    unsigned int sbb, sbe;
    if (target_st > solve_st) {
      sbb = BoardTree::SuccBoardBegin(solve_st, solve_bd, target_st);
      sbe = BoardTree::SuccBoardEnd(solve_st, solve_bd, target_st);
    } else {
      sbb = solve_bd;
      sbe = solve_bd + 1;
    }
    for (unsigned int target_bd = sbb; target_bd < sbe; ++target_bd) {
#if 0
      fprintf(stderr, "P%u base solve nt %u base target nt %u endgame nt %u "
	      "solve_bd %u target_bd %u\n", p, base_solve_nt,
	      base_target_nt, endgame_node->NonterminalID(), solve_bd,
	      target_bd);
#endif
      solver->Solve(endgame_solve_root, endgame_node, base_solve_root,
		    action_sequence, solve_bd, target_bd,
		    base_node->NonterminalID(), num_its, endgame_betting_tree);
    }
    return;
  }
  unsigned int num_succs = base_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = base_node->ActionName(s);
    WalkTrunk2(base_node->IthSucc(s), endgame_node->IthSucc(s),
	       action_sequence + action, solver, endgame_betting_tree,
	       target_st, num_its, base_solve_root, endgame_solve_root,
	       solve_st, solve_bd);
  }
}

static void WalkTrunk1(Node *base_node, Node *endgame_node,
		       const string &action_sequence, EndgameSolver *solver,
		       BettingTree *endgame_betting_tree,
		       unsigned int solve_st, unsigned int target_st,
		       unsigned int num_its) {
  if (base_node->Terminal()) return;
  unsigned int st = base_node->Street();
  if (st == solve_st) {
    unsigned int num_solve_boards = BoardTree::NumBoards(st);
    for (unsigned int sbd = 0; sbd < num_solve_boards; ++sbd) {
      WalkTrunk2(base_node, endgame_node, action_sequence, solver,
		 endgame_betting_tree, target_st, num_its, base_node,
		 endgame_node, solve_st, sbd);
    }
    return;
  }
  unsigned int num_succs = base_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = base_node->ActionName(s);
    WalkTrunk1(base_node->IthSucc(s), endgame_node->IthSucc(s),
	       action_sequence + action, solver, endgame_betting_tree,
	       solve_st, target_st, num_its);
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card params> "
	  "<endgame card params> <base betting params> "
	  "<endgame betting params> <base CFR params> <endgame CFR params> "
	  "<solve street> <target street> <base it> <num endgame its> "
	  "[unsafe|cfrd|maxmargin|combined] [cbrs|cfrs] [card|bucket] "
	  "[zerosum|raw]\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 16) Usage(argv[0]);
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
  unsigned int solve_street, target_street, base_it, num_endgame_its;
  if (sscanf(argv[8], "%u", &solve_street) != 1)     Usage(argv[0]);
  if (sscanf(argv[9], "%u", &target_street) != 1)    Usage(argv[0]);
  if (sscanf(argv[10], "%u", &base_it) != 1)         Usage(argv[0]);
  if (sscanf(argv[11], "%u", &num_endgame_its) != 1) Usage(argv[0]);
  string m = argv[12];
  ResolvingMethod method;
  if (m == "unsafe")         method = ResolvingMethod::UNSAFE;
  else if (m == "cfrd")      method = ResolvingMethod::CFRD;
  else if (m == "maxmargin") method = ResolvingMethod::MAXMARGIN;
  else if (m == "combined")  method = ResolvingMethod::COMBINED;
  else                       Usage(argv[0]);
  string v = argv[13];
  bool cfrs;
  if (v == "cbrs")      cfrs = false;
  else if (v == "cfrs") cfrs = true;
  else                  Usage(argv[0]);
  string l = argv[14];
  bool card_level;
  if (l == "card")        card_level = true;
  else if (l == "bucket") card_level = false;
  else                    Usage(argv[0]);
  string z = argv[15];
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
  // Endgame solver gets the betting tree, but it is deleted before the
  // endgame solver's destructor gets called.
  EndgameSolver solver(solve_street, base_it, *base_card_abstraction,
		       *endgame_card_abstraction, *base_betting_abstraction,
		       *endgame_betting_abstraction, *base_cfr_config,
		       *endgame_cfr_config, base_buckets, endgame_buckets,
		       base_betting_tree, method, cfrs, card_level, zero_sum,
		       num_threads);
  WalkTrunk1(base_betting_tree->Root(), endgame_betting_tree->Root(), "x",
	     &solver, endgame_betting_tree, solve_street,
	     target_street, num_endgame_its);
  fprintf(stderr, "After WalkTrunk1\n");

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
