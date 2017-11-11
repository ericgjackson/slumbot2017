#ifndef _STATELESS_NL_AGENT_H_
#define _STATELESS_NL_AGENT_H_

#include <string>
#include <vector>

#include "acpc_protocol.h"
#include "agent.h"
#include "cards.h"

using namespace std;

class BettingAbstraction;
class Buckets;
class CardAbstraction;
class CFRConfig;
class CFRValues;
class Game;
class HandTree;
class Hands;
class NearestNeighbors;
class Node;
class RuntimeConfig;
class BettingTree;

class NLAgent : public Agent {
 public:
  NLAgent(const CardAbstraction *p0_ca, const CardAbstraction *p1_ca, 
	  const BettingAbstraction *ba, const CFRConfig *cc,
	  const RuntimeConfig *p0_rc, const RuntimeConfig *p1_rc, 
	  BettingTree *p0_tree, BettingTree *p1_tree, bool debug,
	  bool exit_on_error, unsigned int small_blind,
	  unsigned int stack_size);
  virtual ~NLAgent(void);
  virtual BotAction HandleStateChange(const string &match_state,
				      unsigned int *we_bet_to);
  float *CurrentProbs(Node *node, unsigned int b);
  bool ChangedCallToFold(void) const {return changed_call_to_fold_;}
  bool ChangedFoldToCall(void) const {return changed_fold_to_call_;}
  unsigned int NumAlternativeFolded(void) const {
    return num_alternative_folded_;
  }
  unsigned int NumAlternativeActualFolded(void) const {
    return num_alternative_actual_folded_;
  }
  unsigned int NumAlternativeCalled(void) const {
    return num_alternative_called_;
  }
  double FracAlternativeFolded(void) const {
    return frac_alternative_folded_;
  }
  double FracAlternativeCalled(void) const {
    return frac_alternative_called_;
  }
  unsigned int NumNeighborFolded(void) const {
    return num_neighbor_folded_;
  }
  double FracNeighborFolded(void) const {
    return frac_neighbor_folded_;
  }
  unsigned int NumAlternativeBets(void) const {
    return num_alternative_bets_;
  }
 protected:
  void Recurse(const vector<Node *> *path, unsigned int path_index,
	       Node *parent, Node *alt_node, unsigned int pa, bool smaller,
	       bool changed, vector<Node *> *alternative_bet_nodes);
  void GetAlternativeBetNodes(const vector<Node *> *path, bool p1, bool smaller,
			      vector<Node *> *alternative_bet_nodes);
  void GetTwoClosestSuccs(Node *node, unsigned int actual_bet_to,
			  unsigned int *below_succ, unsigned int *below_bet_to,
			  unsigned int *above_succ,
			  unsigned int *above_bet_to);
  double BelowProb(unsigned int actual_bet_to, unsigned int below_bet_to,
		   unsigned int above_bet_to, unsigned int actual_pot_size);
  void Interpret(Action a, vector<Node *> *path, Node *sob_node,
		 unsigned int last_bet_to, unsigned int opp_bet_amount,
		 bool *forced_all_in);
  void Translate(Action a, vector<Node *> *path, Node **sob_node,
		 unsigned int actual_pot_size, unsigned int hand_index);
  void ProcessActions(vector<Action> *actions, unsigned int pa,
		      unsigned int hand_index, vector<Node *> *path,
		      Node **sob_node, bool *terminate,
		      unsigned int *last_bet_to);
  bool StatelessForceAllIn(Node *last_node,
			   unsigned int actual_opp_bet_to);
  bool AreWeAllIn(vector<Action> *actions);
  void WhoseAction(vector<Action> *actions, bool *p1_to_act, bool *p2_to_act);
  bool AreWeFacingBet(const vector<Action> *actions, unsigned int *opp_bet_to,
		      unsigned int *opp_bet_amount);
  void UpdateCards(int street, Card our_hi, Card our_lo, Card *raw_board,
		   unsigned int ***current_buckets);
  bool HandleRaise(Node *bet_node, unsigned int hand_index,
		   unsigned int ***current_buckets, vector<Node *> *path);
  double *GetActionProbs(const vector<Action> &actions,
			 vector<Node *> *path, Node *raise_node, 
			 unsigned int hand_index,
			 unsigned int ***current_buckets, bool p1,
			 unsigned int *opp_bet_amount, bool *force_all_in,
			 bool *force_call);

  BettingTree *p0_tree_;
  BettingTree *p1_tree_;
  bool debug_;
  bool exit_on_error_;
  bool respect_pot_frac_;
  unsigned int small_blind_;
  unsigned int stack_size_;
  Hands **hands_;
  CFRValues **probs_;
  Buckets *p0_buckets_;
  Buckets *p1_buckets_;
  bool *fold_to_alternative_streets_;
  bool *call_alternative_streets_;
  bool ftann_;
  bool eval_overrides_;
  unsigned int override_min_pot_size_;
  unsigned int min_neighbor_folds_;
  double min_neighbor_frac_;
  unsigned int min_alternative_folds_;
  unsigned int min_actual_alternative_folds_;
  double min_frac_alternative_folded_;
  bool prior_alternatives_;
  bool changed_call_to_fold_;
  bool changed_fold_to_call_;
  unsigned int num_alternative_folded_;
  unsigned int num_alternative_actual_folded_;
  unsigned int num_alternative_called_;
  double frac_alternative_folded_;
  double frac_alternative_called_;
  unsigned int num_neighbor_folded_;
  double frac_neighbor_folded_;
  unsigned int num_alternative_bets_;
  bool translate_to_larger_;
  bool translate_bet_to_call_;
  bool no_small_bets_;
  unsigned int translation_method_;
  bool hard_coded_root_strategy_;
  bool hard_coded_r200_strategy_;
  bool hard_coded_r250_strategy_;
  bool hard_coded_r200r600_strategy_;
  bool hard_coded_r200r800_strategy_;
  NearestNeighbors **nearest_neighbors_;
};

#endif
