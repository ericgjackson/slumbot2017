#ifndef _RUNTIME_CONFIG_H_
#define _RUNTIME_CONFIG_H_

#include <string>
#include <vector>

using namespace std;

class Params;

class RuntimeConfigInstance {
 public:
  RuntimeConfigInstance(Params *params);
  const string &RuntimeConfigName(void) const {return runtime_config_name_;}
  unsigned long long int Iteration(void) const {return iteration_;}
  const vector<unsigned int> &QuantizedStreets(void) const {
    return quantized_streets_;
  }
  double MinProb(void) const {return min_prob_;}
  double FoldRoundUp(void) const {return fold_round_up_;}
  double FoldRoundDown(void) const {return fold_round_down_;}
  double QMinProb(void) const {return q_min_prob_;}
  double QFoldRoundUp(void) const {return q_fold_round_up_;}
  bool Purify(void) const {return purify_;}
  bool SampledPurify(void) const {return sampled_purify_;}
  bool ProbsInMemory(void) const {return probs_in_memory_;}
  const vector<unsigned int> &BucketMemoryStreets(void) const {
    return bucket_memory_streets_;
  }
  bool RespectPotFrac(void) const {return respect_pot_frac_;}
  bool UseSupervisor(void) const {return use_supervisor_;}
  const vector<unsigned int> &FoldToAlternativeStreets(void) const {
    return fold_to_alternative_streets_;
  }
  const vector<unsigned int> &CallAlternativeStreets(void) const {
    return call_alternative_streets_;
  }
  bool FTANN(void) const {return ftann_;}
  bool EvalOverrides(void) const {return eval_overrides_;}
  unsigned int OverrideMinPotSize(void) const {return override_min_pot_size_;}
  unsigned int MinNeighborFolds(void) const {return min_neighbor_folds_;}
  double MinNeighborFrac(void) const {return min_neighbor_frac_;}
  unsigned int MinAlternativeFolds(void) const {return min_alternative_folds_;}
  unsigned int MinActualAlternativeFolds(void) const {
    return min_actual_alternative_folds_;
  }
  double MinFracAlternativeFolded(void) const {
    return min_frac_alternative_folded_;
  }
  bool PriorAlternatives(void) const {return prior_alternatives_;}
  bool FewerAllInBets(void) const {return fewer_all_in_bets_;}
  bool TranslateToLarger(void) const {return translate_to_larger_;}
  bool TranslateBetToCall(void) const {return translate_bet_to_call_;}
  bool NoSmallBets(void) const {return no_small_bets_;}
  unsigned int TranslationMethod(void) const {return translation_method_;}
#if 0
  ProbType PreflopProbType(void) const {return preflop_prob_type_;}
  ProbType FlopProbType(void) const {return flop_prob_type_;}
  ProbType TurnProbType(void) const {return turn_prob_type_;}
  ProbType RiverProbType(void) const {return river_prob_type_;}
  ProbSource PreflopSource(void) const {return preflop_source_;}
  ProbSource FlopSource(void) const {return flop_source_;}
  ProbSource TurnSource(void) const {return turn_source_;}
  ProbSource RiverSource(void) const {return river_source_;}
#endif
  unsigned int NumToBlend(void) const {return num_to_blend_;}
  bool HardCodedRootStrategy(void) const {return hard_coded_root_strategy_;}
  bool HardCodedR200Strategy(void) const {return hard_coded_r200_strategy_;}
  bool HardCodedR250Strategy(void) const {return hard_coded_r250_strategy_;}
  bool HardCodedR200R600Strategy(void) const {
    return hard_coded_r200r600_strategy_;
  }
  bool HardCodedR200R800Strategy(void) const {
    return hard_coded_r200r800_strategy_;
  }
  bool NearestNeighbors(void) const {return nearest_neighbors_;}
  bool NNDisk(void) const {return nn_disk_;}

  void SetIteration(unsigned long long int it) {iteration_ = it;}

 private:
  string runtime_config_name_;
  unsigned long long int iteration_;
  vector<unsigned int> quantized_streets_;
  double min_prob_;
  double fold_round_up_;
  double fold_round_down_;
  double q_min_prob_;
  double q_fold_round_up_;
  bool purify_;
  bool sampled_purify_;
  bool probs_in_memory_;
  vector<unsigned int> bucket_memory_streets_;
  bool respect_pot_frac_;
  bool use_supervisor_;
  vector<unsigned int> fold_to_alternative_streets_;
  vector<unsigned int> call_alternative_streets_;
  bool ftann_;
  bool eval_overrides_;
  unsigned int override_min_pot_size_;
  unsigned int min_neighbor_folds_;
  double min_neighbor_frac_;
  unsigned int min_alternative_folds_;
  unsigned int min_actual_alternative_folds_;
  double min_frac_alternative_folded_;
  bool prior_alternatives_;
  bool fewer_all_in_bets_;
  bool translate_to_larger_;
  bool translate_bet_to_call_;
  bool no_small_bets_;
  unsigned int translation_method_;
#if 0
  ProbType preflop_prob_type_;
  ProbType flop_prob_type_;
  ProbType turn_prob_type_;
  ProbType river_prob_type_;
  ProbSource preflop_source_;
  ProbSource flop_source_;
  ProbSource turn_source_;
  ProbSource river_source_;
#endif
  unsigned int num_to_blend_;
  bool hard_coded_root_strategy_;
  bool hard_coded_r200_strategy_;
  bool hard_coded_r250_strategy_;
  bool hard_coded_r200r600_strategy_;
  bool hard_coded_r200r800_strategy_;
  bool nearest_neighbors_;
  bool nn_disk_;
};

class RuntimeConfig {
 public:
  static void Initialize(Params *params);
  static const RuntimeConfigInstance *Instance(void) {return instance_;}
  static const string &RuntimeConfigName(void) {
    return instance_->RuntimeConfigName();
  }
  static unsigned long long int Iteration(void) {return instance_->Iteration();}
  static const vector<unsigned int> &QuantizedStreets(void) {
    return instance_->QuantizedStreets();
  }
  static double MinProb(void) {return instance_->MinProb();}
  static double FoldRoundUp(void) {return instance_->FoldRoundUp();}
  static double FoldRoundDown(void) {return instance_->FoldRoundDown();}
  static double QMinProb(void) {return instance_->QMinProb();}
  static double QFoldRoundUp(void) {return instance_->QFoldRoundUp();}
  static bool Purify(void) {return instance_->Purify();}
  static bool SampledPurify(void) {return instance_->SampledPurify();}
  static bool ProbsInMemory(void) {return instance_->ProbsInMemory();}
  static const vector<unsigned int> &BucketMemoryStreets(void) {
    return instance_->BucketMemoryStreets();
  }
  static bool RespectPotFrac(void) {return instance_->RespectPotFrac();}
  static bool UseSupervisor(void) {return instance_->UseSupervisor();}
  static const vector<unsigned int> &FoldToAlternativeStreets(void) {
    return instance_->FoldToAlternativeStreets();
  }
  static const vector<unsigned int> &CallAlternativeStreets(void) {
    return instance_->CallAlternativeStreets();
  }
  static bool FTANN(void) {return instance_->FTANN();}
  static bool EvalOverrides(void) {return instance_->EvalOverrides();}
  static unsigned int OverrideMinPotSize(void) {
    return instance_->OverrideMinPotSize();
  }
  static unsigned int MinNeighborFolds(void) {
    return instance_->MinNeighborFolds();
  }
  static unsigned int MinNeighborFrac(void) {
    return instance_->MinNeighborFrac();
  }
  static unsigned int MinAlternativeFolds(void) {
    return instance_->MinAlternativeFolds();
  }
  static unsigned int MinActualAlternativeFolds(void) {
    return instance_->MinActualAlternativeFolds();
  }
  static double MinFracAlternativeFolded(void) {
    return instance_->MinFracAlternativeFolded();
  }
  static bool PriorAlternatives(void) {return instance_->PriorAlternatives();}
  static bool TranslateToLarger(void) {return instance_->TranslateToLarger();}
  static bool TranslateBetToCall(void) {
    return instance_->TranslateBetToCall();
  }
  static bool NoSmallBets(void) {return instance_->NoSmallBets();}
  static const unsigned int TranslationMethod(void) {
    return instance_->TranslationMethod();
  }
#if 0
  static ProbType PreflopProbType(void) {
    return instance_->PreflopProbType();
  }
  static ProbType FlopProbType(void) {
    return instance_->FlopProbType();
  }
  static ProbType TurnProbType(void) {
    return instance_->TurnProbType();
  }
  static ProbType RiverProbType(void) {
    return instance_->RiverProbType();
  }
  static ProbSource PreflopSource(void) {return instance_->PreflopSource();}
  static ProbSource FlopSource(void) {return instance_->FlopSource();}
  static ProbSource TurnSource(void) {return instance_->TurnSource();}
  static ProbSource RiverSource(void) {return instance_->RiverSource();}
#endif
  static void SetIteration(unsigned long long int it) {
    instance_->SetIteration(it);
  }
  static unsigned int NumToBlend(void) {return instance_->NumToBlend();}
  static bool HardCodedRootStrategy(void) {
    return instance_->HardCodedRootStrategy();
  }
  static bool HardCodedR200Strategy(void) {
    return instance_->HardCodedR200Strategy();
  }
  static bool HardCodedR250Strategy(void) {
    return instance_->HardCodedR250Strategy();
  }
  static bool HardCodedR200R600Strategy(void) {
    return instance_->HardCodedR200R600Strategy();
  }
  static bool HardCodedR200R800Strategy(void) {
    return instance_->HardCodedR200R800Strategy();
  }
  static bool NearestNeighbors(void) {return instance_->NearestNeighbors();}
  static bool NNDisk(void) {return instance_->NNDisk();}
 private:
  static RuntimeConfigInstance *instance_;
};

#endif
