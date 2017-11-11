#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

void Show(Node *node, const string &action_sequence,
	  const string &target_action_sequence,
	  const Buckets &buckets, const BettingTree *betting_tree,
	  const CardAbstraction &card_abstraction,
	  const BettingAbstraction &betting_abstraction,
	  const CFRConfig &cfr_config, unsigned int it) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  fprintf(stderr, "%s\n", action_sequence.c_str());
  if (action_sequence == target_action_sequence) {
    unsigned int nt = node->NonterminalID();
    unsigned int pa = node->PlayerActing();
    unsigned int st = node->Street();
    unsigned int dsi = node->DefaultSuccIndex();

    unsigned int num_players = Game::NumPlayers();
    unique_ptr<bool []> players(new bool[num_players]);
    for (unsigned int p = 0; p < num_players; ++p) {
      players[p] = (p == pa);
    }
    unsigned int max_street = Game::MaxStreet();
    unique_ptr<bool []> streets(new bool[max_street + 1]);
    for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
      streets[st1] = (st1 == st);
    }
    CFRValues sumprobs(players.get(), true, streets.get(), betting_tree,
		       0, 0, card_abstraction, buckets, nullptr);
    char dir[500];
    sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(), Game::NumPlayers(),
	    card_abstraction.CardAbstractionName().c_str(),
	    Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	    betting_abstraction.BettingAbstractionName().c_str(),
	    cfr_config.CFRConfigName().c_str());
    sumprobs.Read(dir, it, betting_tree->Root(), "x", kMaxUInt);
    int *i_values = nullptr;
    double *d_values = nullptr;
    if (sumprobs.Ints(pa, st)) {
      sumprobs.Values(pa, st, nt, &i_values);
    } else {
      sumprobs.Values(pa, st, nt, &d_values);
    }
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int num_board_cards = Game::NumBoardCards(st);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      const Card *board = BoardTree::Board(st, bd);
      unsigned int max_card = Game::MaxCard();
      unsigned int hcp = 0;
      for (unsigned int hi = 1; hi <= max_card; ++hi) {
	if (InCards(hi, board, num_board_cards)) continue;
	for (unsigned int lo = 0; lo < hi; ++lo) {
	  if (InCards(lo, board, num_board_cards)) continue;
	  printf("%s ", target_action_sequence.c_str());
	  OutputNCards(board, num_board_cards);
	  printf(" / ");
	  OutputTwoCards(hi, lo);
	  unsigned int h = bd * num_hole_card_pairs + hcp;
	  unsigned int offset;
	  unsigned int b = 0;
	  if (buckets.None(st)) {
	    offset = h * num_succs;
	  } else {
	    b = buckets.Bucket(st, h);
	    offset = b * num_succs;
	  }
	  double sum = 0;
	  if (i_values) {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      sum += i_values[offset + s];
	    }
	  } else {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      sum += d_values[offset + s];
	    }
	  }
	  if (sum == 0) {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      printf(" %f", s == dsi ? 1.0 : 0);
	    }
	  } else {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      if (i_values) {
		printf(" %f", i_values[offset + s] / sum);
	      } else {
		printf(" %f", d_values[offset + s] / sum);
	      }
	    }
	  }
	  printf(" (bd %u hcp %u b %u sum %.1f)\n", bd, hcp, b, sum);
	  fflush(stdout);
	  ++hcp;
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Show(node->IthSucc(s), action_sequence + action, target_action_sequence,
	 buckets, betting_tree, card_abstraction, betting_abstraction,
	 cfr_config, it);
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <action>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 7) Usage(argv[0]);
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
  unique_ptr<CFRConfig>
    cfr_config(new CFRConfig(*cfr_params));
  unsigned int it;
  if (sscanf(argv[5], "%u", &it) != 1) Usage(argv[0]);
  string target_action_sequence = argv[6];

  BoardTree::Create();
  // Excessive to load all buckets.
  Buckets buckets(*card_abstraction, false);
  unique_ptr<BettingTree>
    betting_tree(BettingTree::BuildTree(*betting_abstraction));

  Show(betting_tree->Root(), "x", target_action_sequence, buckets, 
       betting_tree.get(), *card_abstraction, *betting_abstraction,
       *cfr_config, it);
}
