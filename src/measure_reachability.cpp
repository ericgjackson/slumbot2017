#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "canonical_cards.h"
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
#include "runtime_config.h"
#include "runtime_params.h"

using namespace std;

unsigned long long int g_zero_count = 0ULL;
unsigned long long int g_low_count = 0ULL;
unsigned long long int g_reach_count = 0ULL;

static void Walk(Node *node, double *hand_probs, unsigned int bd,
		 unsigned int p, const CFRValues &probs,
		 const HandTree &hand_tree, unsigned int last_st);

static void StreetInitial(Node *node, double *hand_probs, unsigned int pbd,
			  unsigned int p, const CFRValues &probs,
			  const HandTree &hand_tree) {
  unsigned int nst = node->Street();
  unsigned int pst = nst - 1;
  unsigned int nbd_begin = BoardTree::SuccBoardBegin(pst, pbd, nst);
  unsigned int nbd_end = BoardTree::SuccBoardEnd(pst, pbd, nst);
  for (unsigned int nbd = nbd_begin; nbd < nbd_end; ++nbd) {
    Walk(node, hand_probs, nbd, p, probs, hand_tree, nst);
  }
}

static void Walk(Node *node, double *hand_probs, unsigned int bd,
		 unsigned int p, const CFRValues &probs,
		 const HandTree &hand_tree, unsigned int last_st) {
  unsigned int st = node->Street();
  if (node->Terminal()) {
    const CanonicalCards *hands = hand_tree.Hands(st, bd);
    unsigned int max_card1 = Game::MaxCard() + 1;
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double prob = hand_probs[enc];
      // Don't scale up counts for terminal nodes on streets prior to the
      // final street.
      if (prob == 0) {
	++g_zero_count;
      } else if (prob < 0.1) {
	++g_low_count;
      } else {
	++g_reach_count;
      }
    }
    return;
  }
  if (st > last_st) {
    StreetInitial(node, hand_probs, bd, p, probs, hand_tree);
  }
  unsigned int num_succs = node->NumSuccs();
  unsigned int this_p = node->P1Choice();
  if (this_p == p) {
    const CanonicalCards *hands = hand_tree.Hands(st, bd);
    unsigned int nt = node->NonterminalID();
    unsigned int dsi = node->DefaultSuccIndex();
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int max_card1 = Game::MaxCard() + 1;
    unsigned int num_enc = max_card1 * max_card1;
    double **succ_hand_probs = new double *[num_succs];
    for (unsigned int s = 0; s < num_succs; ++s) {
      succ_hand_probs[s] = new double[num_enc];
      for (unsigned int i = 0; i < num_enc; ++i) succ_hand_probs[s][i] = 0;
    }
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double prob = hand_probs[enc];
      if (prob == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  succ_hand_probs[s][enc] = 0;
	}
      } else {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  double this_prob = probs.Prob(p, st, nt, bd, i, num_hole_card_pairs,
					s, num_succs, dsi);
	  succ_hand_probs[s][enc] = prob * this_prob;
	}
      }
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      Walk(node->IthSucc(s), succ_hand_probs[s], bd, p, probs, hand_tree, st);
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      delete [] succ_hand_probs[s];
    }
    delete [] succ_hand_probs;
  } else {
    for (unsigned int s = 0; s < num_succs; ++s) {
      Walk(node->IthSucc(s), hand_probs, bd, p, probs, hand_tree, st);
    }
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <betting abstraction params> "
	  "<CFR params> <runtime params> <it>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 6) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[2]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unique_ptr<Params> cfr_params = CreateCFRParams();
  cfr_params->ReadFromFile(argv[3]);
  unique_ptr<CFRConfig>
    cfr_config(new CFRConfig(*cfr_params));
  unique_ptr<Params> runtime_params = CreateRuntimeParams();
  runtime_params->ReadFromFile(argv[4]);
  unique_ptr<RuntimeConfig>
    runtime_config(new RuntimeConfig(*runtime_params));

  unsigned int it;
  if (sscanf(argv[5], "%u", &it) != 1) Usage(argv[0]);
  runtime_config->SetIteration(it);

  BoardTree::Create();
  BettingTree *betting_tree = BettingTree::BuildTree(*betting_abstraction);

  HandTree hand_tree(0, 0, Game::MaxStreet());

  char dir[500];
  sprintf(dir, "%s/%s.null.%u.%u.%u.%s.%s",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction->BettingAbstractionName().c_str(),
	  cfr_config->CFRConfigName().c_str());

  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  double *hand_probs = new double[num_enc];
  for (unsigned int i = 0; i < num_enc; ++i) hand_probs[i] = 1.0;
  for (unsigned int p = 0; p <= 1; ++p) {
    CFRValues probs(p, p^1, true, nullptr, betting_tree->NumNonterminals(),
		    0, 0, kMaxUInt);
    probs.Read(dir, it, betting_tree->Root(), 0, p);
    Walk(betting_tree->Root(), hand_probs, 0, p, probs, hand_tree, 0);
    unsigned long long int total = g_zero_count + g_low_count + g_reach_count;
    double d_total = total;
    printf("Player %u\n", p);
    printf("  Zero:  %.2f%% (%llu)\n", 100.0 * g_zero_count / d_total,
	   g_zero_count);
    printf("  Low:   %.2f%% (%llu)\n", 100.0 * g_low_count / d_total,
	   g_low_count);
    printf("  Reach: %.2f%% (%llu)\n", 100.0 * g_reach_count / d_total,
	   g_reach_count);
  }
  delete [] hand_probs;
  delete betting_tree;
}
