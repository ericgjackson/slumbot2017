#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "board_tree.h"
#include "buckets.h"
#include "cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <street>\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 4) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_abstraction_params = CreateCardAbstractionParams();
  card_abstraction_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction> ca(new CardAbstraction(*card_abstraction_params));
  unsigned int street;
  if (sscanf(argv[3], "%u", &street) != 1) Usage(argv[0]);
  Buckets buckets(*ca, false);
  unsigned int num_buckets = buckets.NumBuckets(street);
  unsigned long long int *sums = new unsigned long long int[num_buckets];
  unsigned int *nums = new unsigned int[num_buckets];
  for (unsigned int b = 0; b < num_buckets; ++b) {
    sums[b] = 0;
    nums[b] = 0;
  }
  // Just need this to get number of hands
  BoardTree::Create();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  unsigned int num_hands = BoardTree::NumBoards(street) * num_hole_card_pairs;
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
      printf("%f %u\n", avg, b);
    }
  }
}
