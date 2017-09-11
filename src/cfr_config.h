#ifndef _CFR_CONFIG_H_
#define _CFR_CONFIG_H_

#include <string>
#include <vector>

using namespace std;

class Params;

class CFRConfig {
public:
  CFRConfig(const Params &params);
  const string &CFRConfigName(void) const {return cfr_config_name_;}
  const string &Algorithm(void) const {return algorithm_;}
  bool NNR(void) const {return nnr_;}
  const vector<int> &RegretFloors(void) const {return regret_floors_;}
  const vector<int> &RegretCeilings(void) const {return regret_ceilings_;}
  const vector<unsigned int> &SumprobCeilings(void) const {
    return sumprob_ceilings_;
  }
  const vector<double> &RegretScaling(void) const {return regret_scaling_;}
  const vector<double> &SumprobScaling(void) const {return sumprob_scaling_;}
  const vector<int> &FreezeIts(void) const {return freeze_its_;}
  unsigned int SoftWarmup(void) const {return soft_warmup_;}
  unsigned int HardWarmup(void) const {return hard_warmup_;}
  unsigned int SubgameStreet(void) const {return subgame_street_;}
  unsigned int SamplingRate(void) const {return sampling_rate_;}
  const vector<unsigned int> &SumprobStreets(void) const {
    return sumprob_streets_;
  }
  const vector<unsigned int> &PruningThresholds(void) const {
    return pruning_thresholds_;
  }
  const vector<double> &FullProbs(void) const {return full_probs_;}
  bool HVBTable(void) const {return hvb_table_;}
  unsigned int CloseThreshold(void) const {return close_threshold_;}
  bool FTL(void) const {return ftl_;}
  bool SampleOppHands(void) const {return sample_opp_hands_;}
  double Explore(void) const {return explore_;}
  bool Probe(void) const {return probe_;}
  const vector<unsigned int> &QuantizedStreets(void) const {
    return quantized_streets_;
  }
  const vector<unsigned int> &ShortQuantizedStreets(void) const {
    return short_quantized_streets_;
  }
  const vector<unsigned int> &ScaledStreets(void) const {
    return scaled_streets_;
  }
  unsigned int ActiveMod(void) const {return active_mod_;}
  unsigned int NumActiveConditions(void) const {return num_active_conditions_;}
  unsigned int NumActiveStreets(unsigned int c) const {
    return active_streets_[c].size();
  }
  unsigned int ActiveStreet(unsigned int c, unsigned int i) const {
    return active_streets_[c][i];
  }
  unsigned int NumActiveRems(unsigned int c) const {
    return active_rems_[c].size();
  }
  unsigned int ActiveRem(unsigned int c, unsigned int i) const {
    return active_rems_[c][i];
  }
  unsigned int BatchSize(void) const {return batch_size_;}
  unsigned int SaveInterval(void) const {return save_interval_;}
  bool DoubleRegrets(void) const {return double_regrets_;}
  bool DoubleSumprobs(void) const {return double_sumprobs_;}
  const vector<unsigned int> &CompressedStreets(void) const {
    return compressed_streets_;
  }
  unsigned int UseAvgForCurrentIt(void) const {return use_avg_for_current_it_;}
  bool Uniform(void) const {return uniform_;}
 private:
  string cfr_config_name_;
  string algorithm_;
  bool nnr_;
  vector<int> regret_floors_;
  vector<int> regret_ceilings_;
  vector<unsigned int> sumprob_ceilings_;
  vector<double> regret_scaling_;
  vector<double> sumprob_scaling_;
  vector<int> freeze_its_;
  unsigned int soft_warmup_;
  unsigned int hard_warmup_;
  unsigned int subgame_street_;
  unsigned int sampling_rate_;
  vector<unsigned int> sumprob_streets_;
  vector<unsigned int> pruning_thresholds_;
  vector<double> full_probs_;
  bool hvb_table_;
  unsigned int close_threshold_;
  bool ftl_;
  bool sample_opp_hands_;
  double explore_;
  bool probe_;
  vector<unsigned int> quantized_streets_;
  vector<unsigned int> short_quantized_streets_;
  vector<unsigned int> scaled_streets_;
  unsigned int active_mod_;
  unsigned int num_active_conditions_;
  vector<unsigned int> *active_streets_;
  vector<unsigned int> *active_rems_;
  unsigned int batch_size_;
  unsigned int save_interval_;
  bool double_regrets_;
  bool double_sumprobs_;
  vector<unsigned int> compressed_streets_;
  unsigned int use_avg_for_current_it_;
  bool uniform_;
};

#endif
