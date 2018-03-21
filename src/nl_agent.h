#ifndef _NL_AGENT_H_
#define _NL_AGENT_H_

#include <memory>
#include <string>
#include <vector>

#include "acpc_protocol.h"
#include "agent.h"
#include "cards.h"

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class BucketsFile;
class CanonicalCards;
class CardAbstraction;
class CFRConfig;
class CFRValues;
class CFRValuesFile;
class DynamicCBR2;
class Game;
class HandTree;
class Hands;
class Node;
class RuntimeConfig;

class NLAgent : public Agent {
 public:
  NLAgent(const CardAbstraction &base_ca, const CardAbstraction &endgame_ca,
	  const BettingAbstraction &base_ba,
	  const BettingAbstraction &endgame_ba, const CFRConfig &base_cc,
	  const CFRConfig &endgame_cc, const RuntimeConfig &rc,
	  unsigned int *iterations, BettingTree **betting_trees,
	  unsigned int endgame_st, unsigned int num_endgame_its, bool debug,
	  bool exit_on_error, bool fixed_seed, unsigned int small_blind,
	  unsigned int stack_size);
  virtual ~NLAgent(void);
  BotAction HandleStateChange(const string &match_state,
			      unsigned int *we_bet_to);
  void SetNewHand(void);
  double *CurrentProbs(Node *node, unsigned int h, unsigned int p);
  void ResolveAndWrite(Node *node, unsigned int gbd,
		       const string &action_sequence,
		       double **reach_probs);
  void AllProbs(Node *node, unsigned int s, unsigned int gbd,
		CanonicalCards *hands, unsigned int p, double *probs);
 protected:
  BettingTree *CreateSubtree(Node *node, unsigned int target_p, bool base);
  void ResolveSubgame(unsigned int p, unsigned int bd, double **reach_probs);
  void GetTwoClosestSuccs(Node *node, unsigned int actual_bet_to,
			  unsigned int *below_succ, unsigned int *below_bet_to,
			  unsigned int *above_succ,
			  unsigned int *above_bet_to);
  double BelowProb(unsigned int actual_bet_to, unsigned int below_bet_to,
		   unsigned int above_bet_to, unsigned int actual_pot_size);
  void Translate(Action a, Node **sob_node, unsigned int actual_pot_size);
  unsigned int GetLastActualBetTo(vector<Action> *actions);
  void ProcessActions(vector<Action> *actions, unsigned int we_p,
		      bool endgame, unsigned int *last_actual_bet_to,
		      Node **sob_node);
  unsigned int LastBetSize(vector<Action> *actions);
  bool AreWeAllIn(vector<Action> *actions, unsigned int p);
  unsigned int WhoseAction(vector<Action> *actions);
  unsigned int MSHCPIndex(unsigned int bd, const Card *cards);
  void UpdateCards(int street, Card our_hi, Card our_lo, Card *raw_board,
		   unsigned int *current_buckets, unsigned int *bd);
  bool HandleRaise(Node *bet_node, unsigned int *current_buckets,
		   unsigned int p);
  double *GetActionProbs(const vector<Action> &actions, Node *sob_node, 
			 unsigned int *current_buckets, unsigned int p,
			 bool *force_call);
  double **GetReachProbs(unsigned int bd, unsigned int asym_p);

  const CardAbstraction &base_card_abstraction_;
  const CardAbstraction &endgame_card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const BettingAbstraction &endgame_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
  const CFRConfig &endgame_cfr_config_;
  const RuntimeConfig &runtime_config_;
  unsigned int *iterations_;
  BettingTree **betting_trees_;
  unsigned int endgame_st_;
  unsigned int num_endgame_its_;
  bool debug_;
  bool exit_on_error_;
  bool fixed_seed_;
  bool respect_pot_frac_;
  unsigned int small_blind_;
  unsigned int stack_size_;
  Hands **hands_;
  CFRValuesFile **probs_;
  BucketsFile *buckets_;
  double min_prob_;
  double fold_round_up_;
  bool purify_;
  bool hard_coded_r200_strategy_;
  bool translate_to_larger_;
  bool translate_bet_to_call_;
  bool no_small_bets_;
  unsigned int translation_method_;
  unique_ptr<Buckets> endgame_buckets_;
  unique_ptr<DynamicCBR2> dynamic_cbr_;
  vector<Node *> *path_;
  unsigned int last_hand_index_;
  unsigned int action_index_;
  unsigned int st_;
  unsigned int player_acting_;
  unsigned int num_remaining_;
  unsigned int num_to_act_on_street_;
  unique_ptr<bool []> folded_;
  CFRValues *endgame_sumprobs_;
  BettingTree *endgame_subtree_;
  struct drand48_data *rand_bufs_;
};

#endif
