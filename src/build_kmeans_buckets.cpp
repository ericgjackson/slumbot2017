// Currently we assume all features values are shorts, but we should
// generalize.

#include <stdio.h>
#include <stdlib.h>

#include "board_tree.h"
#include "constants.h"
#include "fast_hash.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "kmeans.h"
#include "params.h"
#include "rand.h"
#include "sparse_and_dense.h"

using namespace std;

static void Write(unsigned int street, const string &bucketing,
		  KMeans *kmeans, unsigned int *indices,
		  unsigned int num_buckets) {
  unsigned int max_street = Game::MaxStreet();
  char buf[500];
  bool short_buckets = num_buckets <= 65536;
  sprintf(buf, "%s/buckets.%s.%i.%i.%i.%s.%i", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), Game::NumSuits(),
	  max_street, bucketing.c_str(), street);
  Writer writer(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  unsigned int num_hands = BoardTree::NumBoards(street) * num_hole_card_pairs;
  for (unsigned int h = 0; h < num_hands; ++h) {
    unsigned int index = indices[h];
    unsigned int b = kmeans->Assignment(index);
    if (short_buckets) {
      if (b > kMaxUnsignedShort) {
	fprintf(stderr, "Bucket %i out of range for short\n", b);
	exit(-1);
      }
      writer.WriteUnsignedShort(b);
    } else {
      writer.WriteUnsignedInt(b);
    }
  }

  sprintf(buf, "%s/num_buckets.%s.%i.%i.%i.%s.%i",
	  Files::StaticBase(), Game::GameName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(), bucketing.c_str(), street);
  Writer writer2(buf);
  writer2.WriteUnsignedInt(num_buckets);
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <street> <num clusters> "
	  "<bucketing> <features> <neighbor thresh> <num iterations> "
	  "<num threads>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 9) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unsigned int street;
  if (sscanf(argv[2], "%u", &street) != 1) Usage(argv[0]);
  unsigned int num_clusters;
  if (sscanf(argv[3], "%u", &num_clusters) != 1) Usage(argv[0]);
  string bucketing = argv[4];
  string features = argv[5];
  double neighbor_thresh;
  if (sscanf(argv[6], "%lf", &neighbor_thresh) != 1) Usage(argv[0]);
  unsigned int num_iterations;
  if (sscanf(argv[7], "%u", &num_iterations) != 1)  Usage(argv[0]);
  unsigned int num_threads;
  if (sscanf(argv[8], "%u", &num_threads) != 1)     Usage(argv[0]);

  // Make clustering deterministic
  SeedRand(0);

  // Just need this to get number of hands
  BoardTree::Create();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  unsigned int num_hands = BoardTree::NumBoards(street) * num_hole_card_pairs;
  fprintf(stderr, "%u hands\n", num_hands);
  unsigned int *indices = new unsigned int[num_hands];

  char buf[500];
  sprintf(buf, "%s/features.%s.%u.%s.%u", Files::StaticBase(),
	  Game::GameName().c_str(), Game::NumRanks(), features.c_str(),
	  street);
  Reader reader(buf);
  unsigned int num_features = reader.ReadUnsignedIntOrDie();
  fprintf(stderr, "%u features\n", num_features);
  short *feature_vals = new short[num_features];
  SparseAndDenseLong *sad = new SparseAndDenseLong;
  uint64_t hash_seed = 0;
  vector<short *> *unique_objects = new vector<short *>;
  for (unsigned int h = 0; h < num_hands; ++h) {
    if (h % 10000000 == 0) {
      fprintf(stderr, "h %u\n", h);
    }
    for (unsigned int f = 0; f < num_features; ++f) {
      feature_vals[f] = reader.ReadShortOrDie();
    }
    unsigned long long int hash = fasthash64((void *)feature_vals,
					     num_features * sizeof(short),
					     hash_seed);
    unsigned int old_num = sad->Num();
    unsigned int index = sad->SparseToDense(hash);
    indices[h] = index;
    unsigned int new_num = sad->Num();
    if (new_num > old_num && new_num % 1000000 == 0) {
      fprintf(stderr, "%u unique feature combos so far\n", new_num);
    }
    if (new_num > old_num) {
      // Previously unseen feature value vector
      short *copy = new short[num_features];
      for (unsigned int f = 0; f < num_features; ++f) {
	copy[f] = feature_vals[f];
      }
      unique_objects->push_back(copy);
    }
    if (sad->Num() != unique_objects->size()) {
      fprintf(stderr, "Size mismatch: %u vs. %u\n", sad->Num(),
	      (unsigned int)unique_objects->size());
      exit(-1);
    }
  }
  delete [] feature_vals;

  unsigned int num_unique = sad->Num();
  if (num_unique != unique_objects->size()) {
    fprintf(stderr, "Final size mismatch: %u vs. %u\n", num_unique,
	    (unsigned int)unique_objects->size());
    exit(-1);
  }
  fprintf(stderr, "%u unique objects\n", num_unique);
  delete sad;

  float **objects = new float *[num_unique];
  for (unsigned int i = 0; i < num_unique; ++i) {
    objects[i] = new float[num_features];
    for (unsigned int f = 0; f < num_features; ++f) {
      objects[i][f] = (*unique_objects)[i][f];
    }
    delete [] (*unique_objects)[i];
  }
  delete unique_objects;

  KMeans kmeans(num_clusters, num_features, num_unique, objects,
		neighbor_thresh, num_threads);
  kmeans.Cluster(num_iterations);
  unsigned int num_actual = kmeans.NumClusters();
  fprintf(stderr, "Num actual buckets: %u\n", num_actual);

  for (unsigned int i = 0; i < num_unique; ++i) {
    delete [] objects[i];
  }
  delete [] objects;

  Write(street, bucketing, &kmeans, indices, num_actual);

  delete [] indices;
}
