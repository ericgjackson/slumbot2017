#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"

using namespace std;

static void Walk(Node *node, CFRValues *sumprobs, const Buckets &buckets,
		 bool *nut_bs, unsigned int st) {
  if (node->Terminal()) return;
  unsigned int st1 = node->Street();
  unsigned int num_succs = node->NumSuccs();
  if (st1 == st && node->PlayerActing() == 1) {
    unsigned int nt = node->NonterminalID();
    unsigned int csi = node->CallSuccIndex();
    unsigned int num_buckets = buckets.NumBuckets(st);
    int *i_values;
    sumprobs->Values(1, st, nt, &i_values);
    for (unsigned int b = 0; b < num_buckets; ++b) {
      if (nut_bs[b]) {
	unsigned long long int sum = 0ULL;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  int sp = i_values[b * num_succs + s];
	  sum += sp;
	}
	int csp = i_values[b * num_succs + csi];
	double call_prob = csp / (double)sum;
	if (call_prob > 0.01) {
	  printf("nt %u b %u p %f csp %i\n", nt, b, call_prob, csp);
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    Walk(node->IthSucc(s), sumprobs, buckets, nut_bs, st);
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <street>\n", prog_name);
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
  unsigned int street;
  if (sscanf(argv[6], "%u", &street) != 1) Usage(argv[0]);
  bool *nut_bs;
  unsigned int num_nut_bs = 0;
  {
    Buckets buckets(*card_abstraction, false);
    unsigned int num_buckets = buckets.NumBuckets(street);
    nut_bs = new bool[num_buckets];
    unsigned long long int *sums = new unsigned long long int[num_buckets];
    unsigned int *nums = new unsigned int[num_buckets];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      sums[b] = 0;
      nums[b] = 0;
      nut_bs[b] = false;
    }
    // Just need this to get number of hands
    BoardTree::Create();
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
    unsigned int num_hands =
      BoardTree::NumBoards(street) * num_hole_card_pairs;
    fprintf(stderr, "%u hands\n", num_hands);

    char buf[500];
    sprintf(buf, "%s/features.%s.%u.hsone.%u", Files::StaticBase(),
	    Game::GameName().c_str(), Game::NumRanks(), street);
    Reader reader(buf);
    unsigned int num_features = reader.ReadUnsignedIntOrDie();
    fprintf(stderr, "%u features\n", num_features);
    for (unsigned int h = 0; h < num_hands; ++h) {
      for (unsigned int f = 0; f < num_features; ++f) {
	short fv = reader.ReadShortOrDie();
	unsigned int b = buckets.Bucket(street, h);
	sums[b] += (unsigned long long int)(fv + 990);
	++nums[b];
      }
    }
    for (unsigned int b = 0; b < num_buckets; ++b) {
      if (nums[b] == 0) {
	fprintf(stderr, "No hands in bucket %u\n", b);
	exit(-1);
      } else  {
	double avg = sums[b] / (double)nums[b];
	if (avg >= 1980) {
	  nut_bs[b] = true;
	  ++num_nut_bs;
	}
      }
    }
    delete [] sums;
    delete [] nums;
  }
  fprintf(stderr, "%u nut buckets\n", num_nut_bs);
  unique_ptr<BettingTree>
    betting_tree(BettingTree::BuildTree(*betting_abstraction));

  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    players[p] = (p == 1);
  }
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> streets(new bool[max_street + 1]);
  for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
    streets[st1] = (st1 == street);
  }
  Buckets buckets(*card_abstraction, true);
  CFRValues sumprobs(players.get(), true, streets.get(), betting_tree.get(),
		     0, 0, *card_abstraction, buckets, nullptr);
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction->CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction->BettingAbstractionName().c_str(),
	  cfr_config->CFRConfigName().c_str());
  sumprobs.Read(dir, it, betting_tree->Root(), "x", kMaxUInt);
  Walk(betting_tree->Root(), &sumprobs, buckets, nut_bs, street);
}
