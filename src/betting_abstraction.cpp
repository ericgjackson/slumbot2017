#include <stdio.h>
#include <stdlib.h>

#include <string>

#include "betting_abstraction.h"
#include "constants.h"
#include "game.h"
#include "params.h"
#include "split.h"

using namespace std;

static void ParseBetSizes(const string &param_value,
			  vector<vector<vector<double> *> *> **bet_sizes) {
  vector<string> v1;
  Split(param_value.c_str(), '|', true, &v1);
  unsigned int v1sz = v1.size();
  *bet_sizes = new vector<vector<vector<double> *> *>(v1sz);
  for (unsigned int st = 0; st < v1sz; ++st) {
    const string &s2 = v1[st];
    if (s2 == "") {
      // No bets whatsoever allowed on this street
      (**bet_sizes)[st] = new vector<vector<double> *>(0);
    } else {
      vector<string> v2;
      Split(s2.c_str(), ';', false, &v2);
      unsigned int v2sz = v2.size();
      (**bet_sizes)[st] = new vector<vector<double> *>(v2sz);
      for (unsigned int i = 0; i < v2sz; ++i) {
	const string &s3 = v2[i];
	vector<string> v3;
	Split(s3.c_str(), ',', false, &v3);
	unsigned int v3sz = v3.size();
	(*(**bet_sizes)[st])[i] = new vector<double>(v3sz);
	for (unsigned int j = 0; j < v3sz; ++j) {
	  double frac;
	  if (sscanf(v3[j].c_str(), "%lf", &frac) != 1) {
	    fprintf(stderr, "Couldn't parse bet sizes: %s\n",
		    param_value.c_str());
	    exit(-1);
	  }
	  (*(*(**bet_sizes)[st])[i])[j] = frac;
	}
      }
    }
  }
}

static void ParseMultipliers(const string &param_value,
			     vector<vector<double> *> **multipliers) {
  vector<string> v1;
  Split(param_value.c_str(), '|', true, &v1);
  unsigned int v1sz = v1.size();
  *multipliers = new vector<vector<double> *>(v1sz);
  for (unsigned int st = 0; st < v1sz; ++st) {
    const string &s2 = v1[st];
    if (s2 == "") {
      // No bets whatsoever allowed on this street
      (**multipliers)[st] = new vector<double>(0);
    } else {
      vector<string> v2;
      Split(s2.c_str(), ';', false, &v2);
      unsigned int v2sz = v2.size();
      (**multipliers)[st] = new vector<double>(v2sz);
      for (unsigned int i = 0; i < v2sz; ++i) {
	double m;
	if (sscanf(v2[i].c_str(), "%lf", &m) != 1) {
	  fprintf(stderr, "Couldn't parse multipliers: %s\n",
		  param_value.c_str());
	  exit(-1);
	}
	(*(**multipliers)[st])[i] = m;
      }
    }
  }
}

static unsigned int *ParseMaxBets(const Params &params, const string &param) {
  if (! params.IsSet(param.c_str())) {
    fprintf(stderr, "%s must be set\n", param.c_str());
    exit(-1);
  }
  const string &pv = params.GetStringValue(param.c_str());
  unsigned int max_street = Game::MaxStreet();
  unsigned int *max_bets = new unsigned int[max_street + 1];
  vector<unsigned int> v;
  ParseUnsignedInts(pv, &v);
  unsigned int num = v.size();
  if (num < max_street + 1) {
    fprintf(stderr, "Expect at least %u max bets values\n", max_street + 1);
    exit(-1);
  }
  for (unsigned int st = 0; st <= max_street; ++st) {
    max_bets[st] = v[st];
  }
  return max_bets;
}

bool **BettingAbstraction::ParseMinBets(const string &value) {
  unsigned int max_street = Game::MaxStreet();
  vector<string> v1;
  Split(value.c_str(), ';', true, &v1);
  if (v1.size() != max_street + 1) {
    fprintf(stderr, "ParseMinBets: expected %u street values\n",
	    max_street + 1);
    exit(-1);
  }
  bool **min_bets = new bool *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int max_bets = max_bets_[st];
    min_bets[st] = new bool[max_bets];
    // Default
    for (unsigned int b = 0; b < max_bets; ++b) {
      min_bets[st][b] = false;
    }
    const string &sv = v1[st];
    vector<string> v2;
    Split(sv.c_str(), ',', false, &v2);
    unsigned int num = v2.size();
    for (unsigned int i = 0; i < num; ++i) {
      const string &sv2 = v2[i];
      unsigned int nb;
      if (sscanf(sv2.c_str(), "%u", &nb) != 1) {
	fprintf(stderr, "ParseMinBets: couldn't parse %s\n", value.c_str());
	exit(-1);
      }
      if (nb >= max_bets) {
	fprintf(stderr, "ParseMinBets: OOB value %u\n", nb);
	exit(-1);
      }
      min_bets[st][nb] = true;
    }
  }
  return min_bets;
}

static unsigned int **ParseMergeRules(const string &value) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  vector<string> v1;
  Split(value.c_str(), ';', true, &v1);
  if (v1.size() != max_street + 1) {
    fprintf(stderr, "ParseMergeRules: expected %u street values\n",
	    max_street + 1);
    exit(-1);
  }
  unsigned int **merge_rules = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    const string &sv = v1[st];
    vector<string> v2;
    Split(sv.c_str(), ',', false, &v2);
    if (v2.size() != num_players + 1) {
      fprintf(stderr,
	      "ParseMergeRules: expected v2 size to be num players + 1\n");
      exit(-1);
    }
    merge_rules[st] = new unsigned int[num_players + 1];
    for (unsigned int p = 0; p <= num_players; ++p) {
      const string &str = v2[p];
      unsigned int nb;
      if (sscanf(str.c_str(), "%u", &nb) != 1) {
	fprintf(stderr, "ParseMergeRules: couldn't parse %s\n", value.c_str());
	exit(-1);
      }
      merge_rules[st][p] = nb;
    }
  }
  return merge_rules;
}

BettingAbstraction::BettingAbstraction(const Params &params) {
  betting_abstraction_name_ = params.GetStringValue("BettingAbstractionName");
  limit_ = params.GetBooleanValue("Limit");
  stack_size_ = params.GetIntValue("StackSize");
  min_bet_ = params.GetIntValue("MinBet");
  unsigned int max_street = Game::MaxStreet();
  all_bet_sizes_ = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    // Default
    all_bet_sizes_[st] = false;
  }
  if (params.IsSet("AllBetSizeStreets")) {
    vector<unsigned int> v;
    ParseUnsignedInts(params.GetStringValue("AllBetSizeStreets"), &v);
    unsigned int num = v.size();
    for (unsigned int i = 0; i < num; ++i) {
      all_bet_sizes_[v[i]] = true;
    }
  }
  all_even_bet_sizes_ = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    // Default
    all_even_bet_sizes_[st] = false;
  }
  if (params.IsSet("AllEvenBetSizeStreets")) {
    vector<unsigned int> v;
    ParseUnsignedInts(params.GetStringValue("AllEvenBetSizeStreets"), &v);
    unsigned int num = v.size();
    for (unsigned int i = 0; i < num; ++i) {
      all_even_bet_sizes_[v[i]] = true;
    }
  }
  initial_street_ = params.GetIntValue("InitialStreet");
  asymmetric_ = params.GetBooleanValue("Asymmetric");

  no_limit_tree_type_ = params.GetIntValue("NoLimitTreeType");

  max_bets_ = nullptr;
  our_max_bets_ = nullptr;
  opp_max_bets_ = nullptr;
  if (no_limit_tree_type_ != 3) {
    if (asymmetric_) {
      our_max_bets_ = ParseMaxBets(params, "OurMaxBets");
      opp_max_bets_ = ParseMaxBets(params, "OppMaxBets");
    } else {
      max_bets_ = ParseMaxBets(params, "MaxBets");
    }
  }

  bool need_bet_sizes = false;
  if (no_limit_tree_type_ != 3) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! all_bet_sizes_[st] && ! all_even_bet_sizes_[st]) {
	need_bet_sizes = true;
	break;
      }
    }
  }
  
  bet_sizes_ = nullptr;
  p0_bet_sizes_ = nullptr;
  p1_bet_sizes_ = nullptr;
  our_bet_sizes_ = nullptr;
  opp_bet_sizes_ = nullptr;
  
  if (need_bet_sizes) {
    if (asymmetric_) {
      if (params.IsSet("BetSizes")) {
	fprintf(stderr, "Use OurBetSizes and OppBetSizes for asymmetric "
		"systems, not BetSizes\n");
	exit(-1);
      }
      if (! params.IsSet("OurBetSizes") || ! params.IsSet("OppBetSizes")) {
	fprintf(stderr, "Expect OurBetSizes and OppBetSizes to be set\n");
	exit(-1);
      }
      ParseBetSizes(params.GetStringValue("OurBetSizes"), &our_bet_sizes_);
      for (unsigned int st = 0; st <= max_street; ++st) {
	if ((*our_bet_sizes_)[st]->size() != our_max_bets_[st]) {
	  fprintf(stderr, "Max bets mismatch\n");
	  exit(-1);
	}
      }
      ParseBetSizes(params.GetStringValue("OppBetSizes"), &opp_bet_sizes_);
      for (unsigned int st = 0; st <= max_street; ++st) {
	if ((*opp_bet_sizes_)[st]->size() != opp_max_bets_[st]) {
	  fprintf(stderr, "Max bets mismatch\n");
	  exit(-1);
	}
      }
    } else {
      if (params.IsSet("P0BetSizes") && params.IsSet("P1BetSizes")) {
	ParseBetSizes(params.GetStringValue("P0BetSizes"), &p0_bet_sizes_);
	for (unsigned int st = 0; st <= max_street; ++st) {
	  if ((*p0_bet_sizes_)[st]->size() != max_bets_[st]) {
	    fprintf(stderr, "Max bets mismatch\n");
	    exit(-1);
	  }
	}
	ParseBetSizes(params.GetStringValue("P1BetSizes"), &p1_bet_sizes_);
	for (unsigned int st = 0; st <= max_street; ++st) {
	  if ((*p1_bet_sizes_)[st]->size() != max_bets_[st]) {
	    fprintf(stderr, "Max bets mismatch\n");
	    exit(-1);
	  }
	}
      } else if (params.IsSet("BetSizes")) {
	ParseBetSizes(params.GetStringValue("BetSizes"), &bet_sizes_);
	for (unsigned int st = 0; st <= max_street; ++st) {
	  if ((*bet_sizes_)[st]->size() != max_bets_[st]) {
	    fprintf(stderr, "Max bets mismatch\n");
	    exit(-1);
	  }
	}
      } else {
	fprintf(stderr, "Expect BetSizes to be set\n");
	exit(-1);
      }
    }
  }

  always_all_in_ = params.GetBooleanValue("AlwaysAllIn");
  our_always_all_in_ = params.GetBooleanValue("OurAlwaysAllIn");
  opp_always_all_in_ = params.GetBooleanValue("OppAlwaysAllIn");

  always_min_bet_ = nullptr;
  our_always_min_bet_ = nullptr;
  opp_always_min_bet_ = nullptr;
  if (params.IsSet("MinBets")) {
    always_min_bet_ = ParseMinBets(params.GetStringValue("MinBets"));
  }
  if (params.IsSet("OurMinBets")) {
    our_always_min_bet_ = ParseMinBets(params.GetStringValue("OurMinBets"));
  }
  if (params.IsSet("OppMinBets")) {
    opp_always_min_bet_ = ParseMinBets(params.GetStringValue("OppMinBets"));
  }
  
  min_all_in_pot_ = params.GetIntValue("MinAllInPot");
  no_open_limp_ = params.GetBooleanValue("NoOpenLimp");
  our_no_open_limp_ = params.GetBooleanValue("OurNoOpenLimp");
  opp_no_open_limp_ = params.GetBooleanValue("OppNoOpenLimp");
  if (params.IsSet("NoRegularBetThreshold")) {
    no_regular_bet_threshold_ = params.GetIntValue("NoRegularBetThreshold");
  } else {
    no_regular_bet_threshold_ = kMaxUInt;
  }
  if (params.IsSet("OurNoRegularBetThreshold")) {
    our_no_regular_bet_threshold_ =
      params.GetIntValue("OurNoRegularBetThreshold");
  } else {
    our_no_regular_bet_threshold_ = kMaxUInt;
  }
  if (params.IsSet("OppNoRegularBetThreshold")) {
    opp_no_regular_bet_threshold_ =
      params.GetIntValue("OppNoRegularBetThreshold");
  } else {
    opp_no_regular_bet_threshold_ = kMaxUInt;
  }
  if (params.IsSet("OnlyPotThreshold")) {
    only_pot_threshold_ = params.GetIntValue("OnlyPotThreshold");
  } else {
    only_pot_threshold_ = kMaxUInt;
  }
  if (params.IsSet("OurOnlyPotThreshold")) {
    our_only_pot_threshold_ = params.GetIntValue("OurOnlyPotThreshold");
  } else {
    our_only_pot_threshold_ = kMaxUInt;
  }
  if (params.IsSet("OppOnlyPotThreshold")) {
    opp_only_pot_threshold_ = params.GetIntValue("OppOnlyPotThreshold");
  } else {
    opp_only_pot_threshold_ = kMaxUInt;
  }
  geometric_type_ = params.GetIntValue("GeometricType");
  our_geometric_type_ = params.GetIntValue("OurGeometricType");
  opp_geometric_type_ = params.GetIntValue("OppGeometricType");
  close_to_all_in_frac_ = params.GetDoubleValue("CloseToAllInFrac");
  if (params.IsSet("OurBetSizeMultipliers")) {
    ParseMultipliers(params.GetStringValue("OurBetSizeMultipliers"),
		     &our_bet_size_multipliers_);
  } else {
    our_bet_size_multipliers_ = nullptr;
  }
  if (params.IsSet("OppBetSizeMultipliers")) {
    ParseMultipliers(params.GetStringValue("OppBetSizeMultipliers"),
		     &opp_bet_size_multipliers_);
  } else {
    opp_bet_size_multipliers_ = nullptr;
  }
  reentrant_streets_.reset(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    // Default
    reentrant_streets_[st] = false;
  }
  if (params.IsSet("ReentrantStreets")) {
    vector<unsigned int> v;
    ParseUnsignedInts(params.GetStringValue("ReentrantStreets"), &v);
    unsigned int num = v.size();
    for (unsigned int i = 0; i < num; ++i) {
      reentrant_streets_[v[i]] = true;
    }
  }
  betting_key_.reset(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    // Default
    betting_key_[st] = false;
  }
  if (params.IsSet("BettingKeyStreets")) {
    vector<unsigned int> v;
    ParseUnsignedInts(params.GetStringValue("BettingKeyStreets"), &v);
    unsigned int num = v.size();
    for (unsigned int i = 0; i < num; ++i) {
      betting_key_[v[i]] = true;
    }
  }
  min_reentrant_pot_ = params.GetIntValue("MinReentrantPot");
  if (params.IsSet("MergeRules")) {
    merge_rules_ = ParseMergeRules(params.GetStringValue("MergeRules"));
  } else {
    merge_rules_ = nullptr;
  }
  if (params.IsSet("AllowableBetTos")) {
    vector<unsigned int> v;
    ParseUnsignedInts(params.GetStringValue("AllowableBetTos"), &v);
    unsigned int num = v.size();
    allowable_bet_tos_.reset(new bool[stack_size_ + 1]);
    for (unsigned int bt = 0; bt <= stack_size_; ++bt) {
      allowable_bet_tos_[bt] = false;
    }
    for (unsigned int i = 0; i < num; ++i) {
      unsigned int bt = v[i];
      if (bt > stack_size_) {
	fprintf(stderr, "OOB bet to %u\n", bt);
	exit(-1);
      }
      allowable_bet_tos_[bt] = true;
    }
  } else {
    allowable_bet_tos_.reset(nullptr);
  }
  last_aggressor_key_ = params.GetBooleanValue("LastAggressorKey");
}

BettingAbstraction::~BettingAbstraction(void) {
  delete [] all_bet_sizes_;
  delete [] max_bets_;
  delete [] our_max_bets_;
  delete [] opp_max_bets_;
  if (merge_rules_) {
    unsigned int max_street = Game::MaxStreet();
    for (unsigned int st = 0; st <= max_street; ++st) {
      delete [] merge_rules_[st];
    }
    delete [] merge_rules_;
  }
}

