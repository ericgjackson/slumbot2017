#ifndef _ACPC_PROTOCOL_H_
#define _ACPC_PROTOCOL_H_

#include <string>
#include <vector>

#include "cards.h"

using namespace std;

enum ActionType {
  CALL,
  FOLD,
  BET
};

struct Action {
  ActionType action_type;
  unsigned int bet_to;
};

void PrintAction(const Action &a);
void ParseActions(const string &action_str, bool limit,
		  bool exit_on_error, vector<Action> *actions);
bool ParseAllActions(const string &action_str, bool limit, bool exit_on_error,
		     vector< vector<Action> * > *actions, unsigned int *street);
bool ParseNoLimitLine(const string &line, const string &player,
		      unsigned int *hand_index, unsigned int *street,
		      vector< vector<Action> * > *actions,
		      Card *our_hi, Card *our_lo, Card *opp_hi, Card *opp_lo,
		      Card *board, double *outcome, bool *button);
bool ParseNoLimitLine(const string &line, unsigned int *street,
		      vector< vector<Action> * > *actions,
		      Card *p2_hi, Card *p2_lo, Card *p1_hi, Card *p1_lo,
		      Card *board, double *p1_outcome, string *p2, string *p1);
bool ParseMatchState(const string &match_state, bool *p1, int *hand_no,
		     string *action, Card *our_hi, Card *our_lo, Card *opp_hi,
		     Card *opp_lo, Card *board, unsigned int *street);

#endif
