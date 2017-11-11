#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "constants.h"
#include "params.h"
#include "runtime_config.h"
#include "split.h"

using namespace std;

RuntimeConfigInstance *RuntimeConfig::instance_ = NULL;

RuntimeConfigInstance::RuntimeConfigInstance(Params *params) {
  runtime_config_name_ = params->GetStringValue("RuntimeConfigName");
  iteration_ = params->GetIntValue("Iteration");
  ParseUnsignedInts(params->GetStringValue("QuantizedStreets"),
		    &quantized_streets_);
  min_prob_ = params->GetDoubleValue("MinProb");
  fold_round_up_ = params->GetDoubleValue("FoldRoundUp");
  fold_round_down_ = params->GetDoubleValue("FoldRoundDown");
  q_min_prob_ = params->GetDoubleValue("QMinProb");
  q_fold_round_up_ = params->GetDoubleValue("QFoldRoundUp");
  purify_ = params->GetBooleanValue("Purify");
  sampled_purify_ = params->GetBooleanValue("SampledPurify");
  probs_in_memory_ = params->GetBooleanValue("ProbsInMemory");
  ParseUnsignedInts(params->GetStringValue("BucketMemoryStreets"),
		    &bucket_memory_streets_);
  respect_pot_frac_ = params->GetBooleanValue("RespectPotFrac");
  use_supervisor_ = params->GetBooleanValue("UseSupervisor");
  ParseUnsignedInts(params->GetStringValue("FoldToAlternativeStreets"),
		    &fold_to_alternative_streets_);
  ParseUnsignedInts(params->GetStringValue("CallAlternativeStreets"),
		    &call_alternative_streets_);
  ftann_ = params->GetBooleanValue("FTANN");
  eval_overrides_ = params->GetBooleanValue("EvalOverrides");
  override_min_pot_size_ = params->GetIntValue("OverrideMinPotSize");
  min_neighbor_folds_ = params->GetIntValue("MinNeighborFolds");
  min_neighbor_frac_ = params->GetDoubleValue("MinNeighborFrac");
  min_alternative_folds_ = params->GetIntValue("MinAlternativeFolds");
  min_actual_alternative_folds_ =
    params->GetIntValue("MinActualAlternativeFolds");
  min_frac_alternative_folded_ =
    params->GetDoubleValue("MinFracAlternativeFolded");
  prior_alternatives_ = params->GetBooleanValue("PriorAlternatives");
  fewer_all_in_bets_ = params->GetBooleanValue("FewerAllInBets");
  translate_to_larger_ = params->GetBooleanValue("TranslateToLarger");
  translate_bet_to_call_ = params->GetBooleanValue("TranslateBetToCall");
  no_small_bets_ = params->GetBooleanValue("NoSmallBets");
  translation_method_ = params->GetIntValue("TranslationMethod");
#if 0
  preflop_prob_type_ = TypeFromName(params->GetStringValue("PreflopProbType"));
  flop_prob_type_ = TypeFromName(params->GetStringValue("FlopProbType"));
  turn_prob_type_ = TypeFromName(params->GetStringValue("TurnProbType"));
  river_prob_type_ = TypeFromName(params->GetStringValue("RiverProbType"));
  if (params->GetStringValue("PreflopProbSource") == "") {
    preflop_source_ = NO_SOURCE;
  } else {
    preflop_source_ =
      SourceFromName(params->GetStringValue("PreflopProbSource"));
  }
  if (params->GetStringValue("FlopProbSource") == "") {
    flop_source_ = NO_SOURCE;
  } else {
    flop_source_ = SourceFromName(params->GetStringValue("FlopProbSource"));
  }
  if (params->GetStringValue("TurnProbSource") == "") {
    turn_source_ = NO_SOURCE;
  } else {
    turn_source_ = SourceFromName(params->GetStringValue("TurnProbSource"));
  }
  if (params->GetStringValue("RiverProbSource") == "") {
    river_source_ = NO_SOURCE;
  } else {
    river_source_ = SourceFromName(params->GetStringValue("RiverProbSource"));
  }
#endif
  num_to_blend_ = params->GetIntValue("NumToBlend");
  hard_coded_root_strategy_ = params->GetBooleanValue("HardCodedRootStrategy");
  hard_coded_r200_strategy_ = params->GetBooleanValue("HardCodedR200Strategy");
  hard_coded_r250_strategy_ = params->GetBooleanValue("HardCodedR250Strategy");
  hard_coded_r200r600_strategy_ =
    params->GetBooleanValue("HardCodedR200R600Strategy");
  hard_coded_r200r800_strategy_ =
    params->GetBooleanValue("HardCodedR200R800Strategy");
  nearest_neighbors_ = params->GetBooleanValue("NearestNeighbors");
  nn_disk_ = params->GetBooleanValue("NNDisk");
}

void RuntimeConfig::Initialize(Params *params) {
  if (instance_ != NULL) {
    fprintf(stderr, "RuntimeConfig::Initialize instance already created\n");
    exit(-1);
  }
  instance_ = new RuntimeConfigInstance(params);
}
