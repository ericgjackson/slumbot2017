#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "cfr_config.h"
#include "constants.h"
#include "game.h"
#include "params.h"
#include "split.h"

using namespace std;

// 1,1;1,1;1,1;0,0
// Semicolons separate values for different streets
// Commas separate values for different players
static bool **ParseSumprobStreets(const string &str) {
  unsigned int num_players = Game::NumPlayers();
  unsigned int max_street = Game::MaxStreet();
  bool **sumprob_streets = new bool *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    sumprob_streets[p] = new bool[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      // Default
      sumprob_streets[p][st] = true;
    }
  }
  if (str == "") return sumprob_streets;
  vector<string> comps1;
  Split(str.c_str(), ';', false, &comps1);
  if (comps1.size() != max_street + 1) {
    fprintf(stderr, "ParseSumprobStreets: Expected %u values\n",
	    max_street + 1);
    exit(-1);
  }
  for (unsigned int st = 0; st <= max_street; ++st) {
    const string &s = comps1[st];
    vector<string> comps2;
    Split(s.c_str(), ',', false, &comps2);
    if (comps2.size() != num_players) {
      fprintf(stderr, "ParseSumprobStreets: Expected %u values\n",
	      num_players);
      exit(-1);
    }
    for (unsigned int p = 0; p < num_players; ++p) {
      const string &s2 = comps2[p];
      if (s2 == "0") {
	sumprob_streets[p][st] = false;
      } else if (s2 == "1") {
	sumprob_streets[p][st] = true;
      } else {
	fprintf(stderr, "Expect 0 or 1 for sumprob streets value\n");
	exit(-1);
      }
    }
  }
  return sumprob_streets;
}

CFRConfig::CFRConfig(const Params &params) {
  cfr_config_name_ = params.GetStringValue("CFRConfigName");
  algorithm_ = params.GetStringValue("Algorithm");
  nnr_ = params.GetBooleanValue("NNR");
  ParseInts(params.GetStringValue("RegretFloors"), &regret_floors_);
  ParseInts(params.GetStringValue("RegretCeilings"), &regret_ceilings_);
  ParseUnsignedInts(params.GetStringValue("SumprobCeilings"),
		    &sumprob_ceilings_);
  ParseDoubles(params.GetStringValue("RegretScaling"), &regret_scaling_);
  ParseDoubles(params.GetStringValue("SumprobScaling"), &sumprob_scaling_);
  ParseInts(params.GetStringValue("FreezeIts"), &freeze_its_);
  if (params.IsSet("SoftWarmup") && params.IsSet("HardWarmup")) {
    fprintf(stderr, "Cannot have both soft and hard warmup\n");
    exit(-1);
  }
  soft_warmup_ = params.GetIntValue("SoftWarmup");
  hard_warmup_ = params.GetIntValue("HardWarmup");
  if (params.IsSet("SubgameStreet")) {
    subgame_street_ = params.GetIntValue("SubgameStreet");
  } else {
    subgame_street_ = kMaxUInt;
  }
  if (params.IsSet("SamplingRate")) {
    sampling_rate_ = params.GetIntValue("SamplingRate");
  }
  sumprob_streets_ =
    ParseSumprobStreets(params.GetStringValue("SumprobStreets"));
  unsigned int max_street = Game::MaxStreet();
  const string &pstr = params.GetStringValue("PruningThresholds");
  if (pstr == "") {
    // Default is no pruning on any street.  kMaxUInt signifies no
    // pruning.
    for (unsigned int st = 0; st <= max_street; ++st) {
      pruning_thresholds_.push_back(kMaxUInt);
    }
  } else {
    ParseUnsignedInts(pstr, &pruning_thresholds_);
    if (pruning_thresholds_.size() != max_street + 1) {
      fprintf(stderr, "Didn't see expected number of pruning thresholds\n");
      exit(-1);
    }
  }
  ParseDoubles(params.GetStringValue("FullProbs"), &full_probs_);
  hvb_table_ = params.GetBooleanValue("HVBTable");
  ftl_ = params.GetBooleanValue("FTL");
  sample_opp_hands_ = params.GetBooleanValue("SampleOppHands");
  explore_ = params.GetDoubleValue("Explore");
  probe_ = params.GetBooleanValue("Probe");
  char_quantized_streets_.reset(new bool[max_street + 1]);
  short_quantized_streets_.reset(new bool[max_street + 1]);
  scaled_streets_.reset(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    char_quantized_streets_[st] = false;
    short_quantized_streets_[st] = false;
    scaled_streets_[st] = false;
  }
  vector<unsigned int> cqsv, sqsv, ssv;
  ParseUnsignedInts(params.GetStringValue("CharQuantizedStreets"), &cqsv);
  unsigned int num_cqsv = cqsv.size();
  for (unsigned int i = 0; i < num_cqsv; ++i) {
    char_quantized_streets_[cqsv[i]] = true;
  }
  ParseUnsignedInts(params.GetStringValue("ShortQuantizedStreets"), &sqsv);
  unsigned int num_sqsv = sqsv.size();
  for (unsigned int i = 0; i < num_sqsv; ++i) {
    short_quantized_streets_[sqsv[i]] = true;
  }
  ParseUnsignedInts(params.GetStringValue("ScaledStreets"), &ssv);
  unsigned int num_ssv = ssv.size();
  for (unsigned int i = 0; i < num_ssv; ++i) {
    scaled_streets_[ssv[i]] = true;
  }
  double_regrets_ = params.GetBooleanValue("DoubleRegrets");
  double_sumprobs_ = params.GetBooleanValue("DoubleSumprobs");
  ParseUnsignedInts(params.GetStringValue("CompressedStreets"),
		    &compressed_streets_);

  close_thresholds_.reset(new unsigned int[max_street + 1]);
  if (params.IsSet("CloseThresholds")) {
    vector<unsigned int> v;
    ParseUnsignedInts(params.GetStringValue("CloseThresholds"), &v);
    unsigned int num = v.size();
    if (num != max_street + 1) {
      fprintf(stderr, "Expected %u values in close thresholds\n",
	      max_street + 1);
      exit(-1);
    }
    for (unsigned int st = 0; st <= max_street; ++st) {
      close_thresholds_[st] = v[st];
    }
  } else {
    for (unsigned int st = 0; st <= max_street; ++st) {
      close_thresholds_[st] = 0;
    }
  }
  active_mod_ = params.GetIntValue("ActiveMod");
  vector<string> conditions;
  Split(params.GetStringValue("ActiveConditions").c_str(), ';', false,
	&conditions);
  num_active_conditions_ = conditions.size();
  if (num_active_conditions_ > 0) {
    active_streets_ = new vector<unsigned int>[num_active_conditions_];
    active_rems_ = new vector<unsigned int>[num_active_conditions_];
    for (unsigned int c = 0; c < num_active_conditions_; ++c) {
      vector<string> comps, streets, rems;
      Split(conditions[c].c_str(), ':', false, &comps);
      if (comps.size() != 2) {
	fprintf(stderr, "Expected two components\n");
	exit(-1);
      }
      Split(comps[0].c_str(), ',', false, &streets);
      Split(comps[1].c_str(), ',', false, &rems);
      if (streets.size() == 0) {
	fprintf(stderr, "Expected at least one street\n");
	exit(-1);
      }
      if (rems.size() == 0) {
	fprintf(stderr, "Expected at least one rem\n");
	exit(-1);
      }
      for (unsigned int i = 0; i < streets.size(); ++i) {
	unsigned int st;
	if (sscanf(streets[i].c_str(), "%u", &st) != 1) {
	  fprintf(stderr, "Couldn't parse street\n");
	  exit(-1);
	}
	active_streets_[c].push_back(st);
      }
      for (unsigned int i = 0; i < rems.size(); ++i) {
	unsigned int rem;
	if (sscanf(rems[i].c_str(), "%u", &rem) != 1) {
	  fprintf(stderr, "Couldn't parse rem\n");
	  exit(-1);
	}
	active_rems_[c].push_back(rem);
      }
    }
  }

  uniform_ = params.GetBooleanValue("Uniform");
}
