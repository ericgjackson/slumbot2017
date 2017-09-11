// Requires the Game object to have been initialized first.

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "card_abstraction.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "io.h"
#include "params.h"
#include "split.h"

using namespace std;

CardAbstraction::CardAbstraction(const Params &params) {
  card_abstraction_name_ = params.GetStringValue("CardAbstractionName");
  Split(params.GetStringValue("Bucketings").c_str(), ',', false,
	&bucketings_);
  unsigned int max_street = Game::MaxStreet();
  if (bucketings_.size() < max_street + 1) {
    fprintf(stderr, "Expected at least %u bucketings\n", max_street + 1);
    exit(-1);
  }
  bucket_thresholds_ = new unsigned int[max_street + 1];
  if (params.IsSet("BucketThresholds")) {
    vector<unsigned int> v;
    ParseUnsignedInts(params.GetStringValue("BucketThresholds"), &v);
    if (v.size() != max_street + 1) {
      fprintf(stderr, "Expected %u values in BucketThresholds\n",
	      max_street + 1);
      exit(-1);
    }
    for (unsigned int st = 0; st <= max_street; ++st) {
      bucket_thresholds_[st] = v[st];
    }
  } else {
    for (unsigned int st = 0; st <= max_street; ++st) {
      bucket_thresholds_[st] = kMaxUInt;
    }
  }
}

CardAbstraction::~CardAbstraction(void) {
}
