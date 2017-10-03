#ifndef _BETTING_ABSTRACTION_H_
#define _BETTING_ABSTRACTION_H_

#include <memory>
#include <string>
#include <vector>

using namespace std;

class Params;

class BettingAbstraction {
 public:
  BettingAbstraction(const Params &params);
  ~BettingAbstraction(void);
  const string &BettingAbstractionName(void) const {
    return betting_abstraction_name_;
  }
  bool Limit(void) const {return limit_;}
  unsigned int StackSize(void) const {return stack_size_;}
  unsigned int MinBet(void) const {return min_bet_;}
  bool AllBetSizeStreet(unsigned int st) const {return all_bet_sizes_[st];}
  bool AllEvenBetSizeStreet(unsigned int st) const {
    return all_even_bet_sizes_[st];
  }
  unsigned int InitialStreet(void) const {return initial_street_;}
  unsigned int MaxBets(unsigned int st, bool our_bet) const {
    if (max_bets_)    return max_bets_[st];
    else if (our_bet) return our_max_bets_[st];
    else              return opp_max_bets_[st];
  }
  unsigned int NumBetSizes(unsigned int st, unsigned int npb,
			   bool our_bet) const {
    if (bet_sizes_)   return (*(*bet_sizes_)[st])[npb]->size();
    else if (our_bet) return (*(*our_bet_sizes_)[st])[npb]->size();
    else              return (*(*opp_bet_sizes_)[st])[npb]->size();
  }
  const vector<double> *BetSizes(unsigned int st, unsigned int npb,
				 bool our_bet) const {
    if (bet_sizes_)   return (*(*bet_sizes_)[st])[npb];
    else if (our_bet) return (*(*our_bet_sizes_)[st])[npb];
    else              return (*(*opp_bet_sizes_)[st])[npb];
  }
  bool Asymmetric(void) const {return asymmetric_;}
  bool AlwaysAllIn(void) const {return always_all_in_;}
  bool OurAlwaysAllIn(void) const {return our_always_all_in_;}
  bool OppAlwaysAllIn(void) const {return opp_always_all_in_;}
  bool AlwaysMinBet(unsigned int st, unsigned int nsb) const {
    if (always_min_bet_ == nullptr) return false;
    return always_min_bet_[st][nsb];
  }
  bool OurAlwaysMinBet(unsigned int st, unsigned int nsb) const {
    if (our_always_min_bet_ == nullptr) return false;
    return our_always_min_bet_[st][nsb];
  }
  bool OppAlwaysMinBet(unsigned int st, unsigned int nsb) const {
    if (opp_always_min_bet_ == nullptr) return false;
    return opp_always_min_bet_[st][nsb];
  }
  unsigned int MinAllInPot(void) const {return min_all_in_pot_;}
  unsigned int NoLimitTreeType(void) const {return no_limit_tree_type_;}
  bool NoOpenLimp(void) const {return no_open_limp_;}
  bool OurNoOpenLimp(void) const {return our_no_open_limp_;}
  bool OppNoOpenLimp(void) const {return opp_no_open_limp_;}
  unsigned int NoRegularBetThreshold(void) const {
    return no_regular_bet_threshold_;
  }
  unsigned int OurNoRegularBetThreshold(void) const {
    return our_no_regular_bet_threshold_;
  }
  unsigned int OppNoRegularBetThreshold(void) const {
    return opp_no_regular_bet_threshold_;
  }
  unsigned int OnlyPotThreshold(void) const {return only_pot_threshold_;}
  unsigned int OurOnlyPotThreshold(void) const {
    return our_only_pot_threshold_;
  }
  unsigned int OppOnlyPotThreshold(void) const {
    return opp_only_pot_threshold_;
  }
  unsigned int GeometricType(void) const {return geometric_type_;}
  unsigned int OurGeometricType(void) const {return our_geometric_type_;}
  unsigned int OppGeometricType(void) const {return opp_geometric_type_;}
  double CloseToAllInFrac(void) const {return close_to_all_in_frac_;}
  double BetSizeMultiplier(unsigned int st, unsigned int npb,
			   bool our_bet) const {
    if (our_bet && our_bet_size_multipliers_) {
      return (*(*our_bet_size_multipliers_)[st])[npb];
    } else if (! our_bet && opp_bet_size_multipliers_) {
      return (*(*opp_bet_size_multipliers_)[st])[npb];
    } else {
      return 0;
    }
  }
  bool BettingKey(unsigned int st) const {
    return betting_key_[st];
  }
  unsigned int MinReentrantPot(void) const {return min_reentrant_pot_;}
 private:
  bool **ParseMinBets(const string &value);
  
  string betting_abstraction_name_;
  bool limit_;
  unsigned int stack_size_;
  unsigned int min_bet_;
  bool *all_bet_sizes_;
  bool *all_even_bet_sizes_;
  unsigned int initial_street_;
  unsigned int *max_bets_;
  unsigned int *our_max_bets_;
  unsigned int *opp_max_bets_;
  vector<vector<vector<double> *> *> *bet_sizes_;
  vector<vector<vector<double> *> *> *our_bet_sizes_;
  vector<vector<vector<double> *> *> *opp_bet_sizes_;
  bool asymmetric_;
  bool always_all_in_;
  bool our_always_all_in_;
  bool opp_always_all_in_;
  bool **always_min_bet_;
  bool **our_always_min_bet_;
  bool **opp_always_min_bet_;
  unsigned int min_all_in_pot_;
  unsigned int no_limit_tree_type_;
  bool no_open_limp_;
  bool our_no_open_limp_;
  bool opp_no_open_limp_;
  unsigned int no_regular_bet_threshold_;
  unsigned int our_no_regular_bet_threshold_;
  unsigned int opp_no_regular_bet_threshold_;
  unsigned int only_pot_threshold_;
  unsigned int our_only_pot_threshold_;
  unsigned int opp_only_pot_threshold_;
  unsigned int geometric_type_;
  unsigned int our_geometric_type_;
  unsigned int opp_geometric_type_;
  double close_to_all_in_frac_;
  vector<vector<double> *> *our_bet_size_multipliers_;
  vector<vector<double> *> *opp_bet_size_multipliers_;
  unique_ptr<bool []> betting_key_;
  unsigned int min_reentrant_pot_;
};

#endif
