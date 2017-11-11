#ifndef _AGENT_H_
#define _AGENT_H_

#include <string>

using namespace std;

class Node;

enum BotAction {
  BA_FOLD,
  BA_CALL,
  BA_BET,
  BA_NONE
};

// There are two types of retrace steps.  If skip_action is 0, a retrace
// step tells us which succ to pursue at a given nonterminal.  If skip action
// is > 0 , we are supposed to ignore an action in the betting sequence.
struct RetraceStep {
  int skip_action; // 1 = skip call; 2 = skip call or fold
  Node *node;
  int succ;
};

class AgentState {
 public:
  AgentState(void) {}
  virtual ~AgentState(void) {}
  int HandNumber(void) const {return hand_number_;}
  void SetHandNumber(int n) {hand_number_ = n;}
  void SetP1(bool p1) {p1_ = p1;}
  bool P1(void) const {return p1_;}
  virtual void AddRetraceStep(int skip_action, Node *node, int succ) = 0;
  virtual unsigned int NumRetraceSteps(void) const = 0;
  virtual RetraceStep IthRetraceStep(unsigned int i) const = 0;
 protected:
  int hand_number_;
  bool p1_;
};

class Agent {
 public:
  Agent(void) {}
  virtual ~Agent(void) {}
  virtual AgentState *NewState(void) {return NULL;}
  virtual BotAction HandleStateChange(const string &match_state,
				      unsigned int *we_bet_to) = 0;
 protected:
};

#endif
