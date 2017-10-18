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
	  const Buckets &buckets, const CFRValues &sumprobs) {
  if (node->Street() == 1) return;
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    unsigned int nt = node->NonterminalID();
    unsigned int pa = node->PlayerActing();
    unsigned int st = node->Street();
    unsigned int dsi = node->DefaultSuccIndex();
    int *i_values = nullptr;
    double *d_values = nullptr;
    if (sumprobs.Ints(pa, st)) {
      sumprobs.Values(pa, st, nt, &i_values);
    } else {
      sumprobs.Values(pa, st, nt, &d_values);
    }
    unsigned int max_card = Game::MaxCard();
    unsigned int hcp = 0;
    for (unsigned int hi = 1; hi <= max_card; ++hi) {
      for (unsigned int lo = 0; lo < hi; ++lo) {
	if (action_sequence == "") {
	  printf("Root ");
	} else {
	  printf("%s ", action_sequence.c_str());
	}
	OutputTwoCards(hi, lo);
	unsigned int offset;
	if (buckets.None(st)) {
	  offset = hcp * num_succs;
	} else {
	  unsigned int b = buckets.Bucket(st, hcp);
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
	printf("\n");
	fflush(stdout);
	++hcp;
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Show(node->IthSucc(s), action_sequence + action, buckets, sumprobs);
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 6) Usage(argv[0]);
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

  // Excessive to load all buckets.  Only need buckets for the preflop.
  Buckets buckets(*card_abstraction, false);
  unique_ptr<BettingTree>
    betting_tree(BettingTree::BuildTree(*betting_abstraction));
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    streets[st] = (st == 0);
  }
  CFRValues sumprobs(nullptr, true, streets.get(), betting_tree.get(), 0, 0,
		     *card_abstraction, buckets, nullptr);
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction->CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction->BettingAbstractionName().c_str(),
	  cfr_config->CFRConfigName().c_str());
  sumprobs.Read(dir, it, betting_tree->Root(), "x", kMaxUInt);
  Show(betting_tree->Root(), "", buckets, sumprobs);
}
