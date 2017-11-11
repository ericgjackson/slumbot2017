#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "card_abstraction.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "io.h"
#include "nearest_neighbors.h"
#include "runtime_config.h"

using namespace std;

NearestNeighbors::NearestNeighbors(const CardAbstraction *ca,
				   const RuntimeConfig *rc,
				   const unsigned int *num_buckets) {
  nn_disk_ = rc->NNDisk();
  unsigned int max_street = Game::MaxStreet();
  unsigned int ms_num_buckets = num_buckets[max_street];
  const string &bucketing = ca->Bucketing(max_street);
  char buf[500];
  sprintf(buf, "%s/nearest_neighbors.%s", Files::StaticBase(),
	  bucketing.c_str());
  reader_ = new Reader(buf);
  unsigned int ms_num_buckets2 = reader_->ReadUnsignedIntOrDie();
  if (ms_num_buckets != ms_num_buckets2) {
    fprintf(stderr, "Num bucket mismatch\n");
    exit(-1);
  }
  max_neighbors_ = reader_->ReadUnsignedIntOrDie();
  if (nn_disk_) {
    offsets_ = new unsigned int[ms_num_buckets];
    neighbors_ = NULL;
    // Read two bytes initially for ms_num_buckets2 and max_neighbors_
    unsigned int cum_bytes = 8;
    for (unsigned int b = 0; b < ms_num_buckets; ++b) {
      offsets_[b] = cum_bytes;
      unsigned int num_n = reader_->ReadUnsignedShortOrDie();
      for (unsigned int i = 0; i < num_n; ++i) reader_->ReadUnsignedIntOrDie();
      cum_bytes += 2 + num_n * 4;
    }
  } else {
    offsets_ = NULL;
    neighbors_ = new unsigned int[ms_num_buckets * max_neighbors_];
    for (unsigned int b = 0; b < ms_num_buckets; ++b) {
      unsigned int num_n = reader_->ReadUnsignedShortOrDie();
      for (unsigned int i = 0; i < num_n; ++i) {
	neighbors_[b * max_neighbors_ + i] = reader_->ReadUnsignedIntOrDie();
      }
      for (unsigned int i = num_n; i < max_neighbors_; ++i) {
	neighbors_[b * max_neighbors_ + i] = kMaxUInt;
      }
    }
    delete reader_;
    reader_ = NULL;
  }
}

NearestNeighbors::~NearestNeighbors(void) {
  delete [] offsets_;
  delete [] neighbors_;
  delete reader_;
}

void NearestNeighbors::GetNeighbors(unsigned int b, vector<unsigned int> *v) {
  v->clear();
  if (nn_disk_) {
    reader_->SeekTo(offsets_[b]);
    unsigned int num_n = reader_->ReadUnsignedShortOrDie();
    for (unsigned int i = 0; i < num_n; ++i) {
      v->push_back(reader_->ReadUnsignedIntOrDie());
    }
  } else {
    for (unsigned int i = 0; i < max_neighbors_; ++i) {
      unsigned int n = neighbors_[b * max_neighbors_ + i];
      if (n != kMaxUInt) v->push_back(n);
    }
  }
}
