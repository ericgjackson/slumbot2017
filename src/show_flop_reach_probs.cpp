#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it>\n", prog_name);
  exit(-1);
}

void Process(Node *node, const Buckets &buckets, CFRValues *sumprobs,
	     double *p0_reach, double *p1_reach, const CanonicalCards *hands) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_players = Game::NumPlayers();
  if (st == 1) {
    for (unsigned int p = 0; p < num_players; ++p) {
      double *reach = p == 0 ? p0_reach : p1_reach;
      double sum = 0;
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	sum += reach[i];
      }
      printf("St %u NT %u P%u sum flop reach probs %f\n", st, nt, p, sum);
    }
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  unsigned int p = node->PlayerActing();
  unsigned int default_succ_index = node->DefaultSuccIndex();
  double *current_probs = new double[num_succs];
  double *d_my_sumprobs = nullptr;
  int *i_my_sumprobs = nullptr;
  if (sumprobs->Ints(p, st)) {
    sumprobs->Values(p, st, nt, &i_my_sumprobs);
  } else {
    sumprobs->Values(p, st, nt, &d_my_sumprobs);
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    double *new_reach = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      if (buckets.None(st)) {
	if (sumprobs->Ints(p, st)) {
	  RegretsToProbs(i_my_sumprobs + i * num_succs, num_succs, true,
			 false, default_succ_index, 0, 0, nullptr,
			 current_probs);
	} else {
	  RegretsToProbs(d_my_sumprobs + i * num_succs, num_succs, true,
			 false, default_succ_index, 0, 0, nullptr,
			 current_probs);
	}
      } else {
	unsigned int b = buckets.Bucket(0, i);
	if (sumprobs->Ints(p, st)) {
	  RegretsToProbs(i_my_sumprobs + b * num_succs, num_succs, true,
			 false, default_succ_index, 0, 0, nullptr,
			 current_probs);
	  if (nt == 1 && p == 0) {
	    const Card *hole_cards = hands->Cards(i);
	    printf("B i %u b %u ", i, b);
	    OutputTwoCards(hole_cards);
	    printf(" s %u prob %f\n", s, current_probs[s]);
	  }
	} else {
	  RegretsToProbs(d_my_sumprobs + b * num_succs, num_succs, true,
			 false, default_succ_index, 0, 0, nullptr,
			 current_probs);
	}
      }
      double sp = current_probs[s];
      if (p == 0) new_reach[i] = p0_reach[i] * sp;
      else        new_reach[i] = p1_reach[i] * sp;
    }
    if (nt == 1 && st == 0 && p == 0 && s == 0) {
      double sum = 0;
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	sum += p1_reach[i];
      }
    }
    if (p == 0) {
      Process(node->IthSucc(s), buckets, sumprobs, new_reach, p1_reach, hands);
    } else {
      Process(node->IthSucc(s), buckets, sumprobs, p0_reach, new_reach, hands);
    }
    delete [] new_reach;
  }
  delete [] current_probs;
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
  unique_ptr<CFRConfig> cfr_config(new CFRConfig(*cfr_params));
  unsigned int it;
  if (sscanf(argv[5], "%u", &it) != 1)          Usage(argv[0]);
  Buckets buckets(*card_abstraction, false);

  unique_ptr<BettingTree> betting_tree;
  if (betting_abstraction->Asymmetric()) {
    fprintf(stderr, "Need to support asymmetric\n");
    exit(-1);
#if 0
    betting_tree.reset(BettingTree::BuildAsymmetricTree(*betting_abstraction,
							target_p == 1));
#endif
  }
  betting_tree.reset(BettingTree::BuildTree(*betting_abstraction));

  HandTree hand_tree(0, 0, 0);
  const CanonicalCards *hands = hand_tree.Hands(0, 0);

  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int max_street = Game::MaxStreet();

  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction->CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction->BettingAbstractionName().c_str(),
	  cfr_config->CFRConfigName().c_str());
  if (betting_abstraction->Asymmetric()) {
    fprintf(stderr, "Need to support asymmetric\n");
    exit(-1);
  }

  bool *streets = new bool[max_street + 1];
  bool *compressed_streets = new bool[max_street + 1];
  streets[0] = true;
  compressed_streets[0] = false;
  for (unsigned int st = 1; st <= max_street; ++st) {
    streets[st] = false;
    compressed_streets[st] = false;
  }
  CFRValues *sumprobs = new CFRValues(nullptr, true, streets,
				      betting_tree.get(), 0, 0,
				      *card_abstraction, buckets,
				      compressed_streets);
  sumprobs->Read(dir, it, betting_tree->Root(),
		 betting_tree->Root()->NonterminalID(), kMaxUInt);
  double *p0_reach = new double[num_hole_card_pairs];
  double *p1_reach = new double[num_hole_card_pairs];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    p0_reach[i] = 1.0;
    p1_reach[i] = 1.0;
  }
  Process(betting_tree->Root(), buckets, sumprobs, p0_reach, p1_reach,
	  hands);
  delete [] p0_reach;
  delete [] p1_reach;
  delete sumprobs;
}
