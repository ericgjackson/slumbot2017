// Takes two bucketings and create a new "crossproduct" bucketing which
// encodes what bucket a hand is in in each of the two input bucketings.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <unordered_map>

#include "board_tree.h"
#include "constants.h"
#include "fast_hash.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"
#include "sparse_and_dense.h"

using namespace std;

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <bucketing1> <bucketing2> "
	  "<new bucketing> <street>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 6) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  string bucketing1 = argv[2];
  string bucketing2 = argv[3];
  string new_bucketing = argv[4];
  unsigned int st;
  if (sscanf(argv[5], "%u", &st) != 1) Usage(argv[0]);
  unsigned int max_street = Game::MaxStreet();
  if (st < 1 || st > max_street) {
    fprintf(stderr, "Street OOB\n");
    exit(-1);
  }

  BoardTree::Create();
  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned long long int num_hands = num_boards * num_hole_card_pairs;
  fprintf(stderr, "num_hands %llu\n", num_hands);

  char buf[500];
  sprintf(buf, "%s/num_buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	  max_street, bucketing2.c_str(), st);
  Reader nb_reader(buf);
  unsigned long long int num_buckets2 = nb_reader.ReadUnsignedIntOrDie();

  sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	  max_street, bucketing1.c_str(), st);
  Reader reader1(buf);
  unsigned long long int file_size1 = reader1.FileSize();
  bool shorts1;
  if (file_size1 == 2 * num_hands) {
    shorts1 = true;
  } else if (file_size1 == 4 * num_hands) {
    shorts1 = false;
  } else {
    fprintf(stderr, "Unexpected file size B: %llu\n", file_size1);
    fprintf(stderr, "File: %s\n", buf);
    exit(-1);
  }

  sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	  max_street, bucketing2.c_str(), st);
  Reader reader2(buf);
  unsigned long long int file_size2 = reader2.FileSize();
  bool shorts2;
  if (file_size2 == 2 * num_hands) {
    shorts2 = true;
  } else if (file_size2 == 4 * num_hands) {
    shorts2 = false;
  } else {
    fprintf(stderr, "Unexpected file size B: %llu\n", file_size2);
    fprintf(stderr, "File: %s\n", buf);
    exit(-1);
  }
  
  unsigned int *buckets = new unsigned int[num_hands];
  SparseAndDenseLong sad;
  for (unsigned long long int h = 0; h < num_hands; ++h) {
    unsigned long long int b1, b2;
    if (shorts1) {
      b1 = reader1.ReadUnsignedShortOrDie();
    } else {
      b1 = reader1.ReadUnsignedIntOrDie();
    }
    if (shorts2) {
      b2 = reader2.ReadUnsignedShortOrDie();
    } else {
      b2 = reader2.ReadUnsignedIntOrDie();
    }
    unsigned long long int sparse = b1 * num_buckets2 + b2;
    unsigned int b = sad.SparseToDense(sparse);
    buckets[h] = b;
  }

  unsigned int num_buckets = sad.Num();
  bool short_buckets = num_buckets < 65536;

  sprintf(buf, "%s/buckets.%s.%u.%u.%u.%s.%u",
	  Files::StaticBase(), Game::GameName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), new_bucketing.c_str(), st);
  Writer writer(buf);
  if (short_buckets) {
    for (unsigned int h = 0; h < num_hands; ++h) {
      writer.WriteUnsignedShort((unsigned short)buckets[h]);
    }
  } else {
    for (unsigned int h = 0; h < num_hands; ++h) {
      writer.WriteUnsignedInt(buckets[h]);
    }
  }

  sprintf(buf, "%s/num_buckets.%s.%u.%u.%u.%s.%u",
	  Files::StaticBase(), Game::GameName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), new_bucketing.c_str(), st);
  Writer writer2(buf);
  writer2.WriteUnsignedInt(num_buckets);
  printf("%u buckets\n", num_buckets);

  delete [] buckets;
}
