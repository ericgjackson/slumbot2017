// Routines for parsing lines of data transmitted using the ACPC protocol.
// Useful both for parsing log files and for parsing messages sent over the
// wire to a bot.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "acpc_protocol.h"
#include "cards.h"
#include "split.h"

using namespace std;

void PrintAction(const Action &a) {
  if (a.action_type == CALL) {
    printf("c");
  } else if (a.action_type == FOLD) {
    printf("f");
  } else if (a.action_type == BET) {
    printf("b to %i", a.bet_to);

  }
}

// Takes a string containing zero or more actions.  Actions may span multiple
// streets in which case there will be '/'s at the street boundaries.
// Doesn't support limit right now (bets are expected to have a following
// amount) but could easily be modified to.
// Not sure if this handles all-in situations where I might get something
// like "cr12342r20000///".
void ParseActions(const string &action_str, bool limit,
		  bool exit_on_error, vector<Action> *actions) {
  char buf[500];
  int i = 0;
  int len = action_str.size();
  while (i < len) {
    Action a;
    char c = action_str[i];
    if (c == '/') {
      ++i;
      continue;
    } else if (c == 'c') {
      // This can also be a check
      a.action_type = CALL;
      a.bet_to = 0;
      ++i;
    } else if (c == 'f') {
      a.action_type = FOLD;
      a.bet_to = 0;
      ++i;
    } else if (c == 'r') {
      ++i;
      a.action_type = BET;
      if (limit) {
      } else {
	int j = i;
	while (j < len && action_str[j] >= '0' && action_str[j] <= '9') ++j;
	memcpy(buf, action_str.c_str() + i, j - i);
	buf[j-i] = 0;
	int amount;
	if (sscanf(buf, "%i", &amount) != 1) {
	  fprintf(stderr, "Couldn't parse bet size j %i i %i\n", j, i);
	  fprintf(stderr, "%s\n", action_str.c_str());
	  if (exit_on_error) {
	    exit(-1);
	  } else {
	    return;
	  }
	}
	a.bet_to = amount;
	i = j;
      }
    } else {
      fprintf(stderr, "ParseActions: unrecognized char %c\n", c);
      if (exit_on_error) exit(-1);
      else               return;
    }
    actions->push_back(a);
  }
}

// Different than the Split() in split.cpp because a string-final
// separator leads to a empty string component.
static void MySplit(const char *line, char sep, vector<string> *comps) {
  comps->clear();
  int len = strlen(line);
  int i = 0;
  while (i < len) {
    int j = i;
    while (i < len && line[i] != sep) ++i;
    string s(line, j, i - j);
    // Strip any trailing \n
    if (s.size() > 0) {
      if (s[s.size() - 1] == '\n') {
	s.resize(s.size() - 1);
      }
    }
    comps->push_back(s);
    if (i == len) break;
    ++i; // Skip past separating character
  }
  if (line[len-1] == '/') {
    comps->push_back("");
  }
}

// Takes an action string which contains all actions for hands.  Returns
// actions separated by street.  Street returned is the final street reached.
bool ParseAllActions(const string &action_str, bool limit, bool exit_on_error,
		     vector< vector<Action> * > *actions,
		     unsigned int *street) {
  actions->clear();

  vector<string> action_comps;
  MySplit(action_str.c_str(), '/', &action_comps);
  *street = action_comps.size() - 1;
  for (unsigned int s = 0; s <= *street; ++s) {
    vector<Action> *street_actions = new vector<Action>;
    ParseActions(action_comps[s], limit, exit_on_error, street_actions);
    actions->push_back(street_actions);
  }
  return true;
}

// The card string contains one or two hole card pairs and also between zero
// and five coard cards.  Examples:
//   AdAc|KsKh/4s3h2d/8c/7d
//   AdAc|KsKh/4s3h2d
//   AdAc|
//   |KsKh
//   AdAc|/4s3h2d
// Hole cards and flop cards returned are sorted from high to low.
static bool ParseCardString(const string &cards, Card *p2_hi, Card *p2_lo,
			    Card *p1_hi, Card *p1_lo, Card *board,
			    unsigned int *street) {
  *p2_hi = 0;
  *p2_lo = 0;
  *p1_hi = 0;
  *p1_lo = 0;
  *street = 0;
  for (int i = 0; i < 5; ++i) board[i] = 0;
  vector<string> card_groups;
  Split(cards.c_str(), '/', false, &card_groups);
  if (card_groups.size() < 1) {
    fprintf(stderr, "Couldn't parse card groups\n");
    fprintf(stderr, "%s\n", cards.c_str());
    return false;
  }
  if (card_groups.size() > 4) {
    fprintf(stderr, "Too many card groups?!?\n");
    fprintf(stderr, "%s\n", cards.c_str());
    return false;
  }
  vector<string> hole_card_strings;
  const string &hole_card_pairs = card_groups[0];
  if (hole_card_pairs.size() == 9) {
    Split(hole_card_pairs.c_str(), '|', false, &hole_card_strings);
  } else if (hole_card_pairs.size() == 5) {
    if (hole_card_pairs[0] == '|') {
      hole_card_strings.push_back("");
      hole_card_strings.push_back(string(hole_card_pairs, 1, 4));
    } else if (hole_card_pairs[4] == '|') {
      hole_card_strings.push_back(string(hole_card_pairs, 0, 4));
      hole_card_strings.push_back("");
    }
  }
  if (hole_card_strings.size() != 2) {
    fprintf(stderr, "Couldn't parse hole card pairs\n");
    fprintf(stderr, "%s\n", cards.c_str());
    fprintf(stderr, "%s\n", hole_card_pairs.c_str());
    return false;
  }
  for (int i = 0; i < 2; ++i) {
    if (hole_card_strings[i] == "") continue;
    Card c1 = ParseCard(hole_card_strings[i].c_str());
    Card c2 = ParseCard(hole_card_strings[i].c_str() + 2);
    Card hi, lo;
    if (c1 > c2) { hi = c1; lo = c2; }
    else         { hi = c2; lo = c1; }
    if (i == 0) { *p2_hi = hi; *p2_lo = lo; }
    else        { *p1_hi = hi; *p1_lo = lo; }
  }

  if (card_groups.size() >= 2) {
    *street = 1;
    // Parse flop.  Order flop from high to low.
    Card c1 = ParseCard(card_groups[1].c_str());
    Card c2 = ParseCard(card_groups[1].c_str() + 2);
    Card c3 = ParseCard(card_groups[1].c_str() + 4);
    if (c1 > c2 && c1 > c3 && c2 > c3) {
      board[0] = c1; board[1] = c2; board[2] = c3;
    } else if (c1 > c2 && c1 > c3 && c3 > c2) {
      board[0] = c1; board[1] = c3; board[2] = c2;
    } else if (c2 > c1 && c2 > c3 && c1 > c3) {
      board[0] = c2; board[1] = c1; board[2] = c3;
    } else if (c2 > c1 && c2 > c3 && c3 > c1) {
      board[0] = c2; board[1] = c3; board[2] = c1;
    } else if (c3 > c1 && c3 > c2 && c1 > c2) {
      board[0] = c3; board[1] = c1; board[2] = c2;
    } else if (c3 > c1 && c3 > c2 && c2 > c1) {
      board[0] = c3; board[1] = c2; board[2] = c1;
    }
  }

  if (card_groups.size() >= 3) {
    *street = 2;
    board[3] = ParseCard(card_groups[2].c_str());
  }

  if (card_groups.size() >= 4) {
    *street = 3;
    board[4] = ParseCard(card_groups[3].c_str());
  }

  return true;
}

// Log lines have six components (when you split the line using ':' as a
// separator):
// 1) STATE
// 2) Hand number
// 3) The betting action
// 4) The cards
// 5) The outcome
// 6) The players
// p1 is the button.  His data (outcome and hole cards) appear as the second
// element of each pair of values.
// We want to be able to handle log files that have been processed to reduce
// all-in variance, so we return a double for outcome, not an int.
bool ParseNoLimitLine(const string &line, const string &player,
		      unsigned int *hand_index, unsigned int *street,
		      vector< vector<Action> * > *actions,
		      Card *our_hi, Card *our_lo, Card *opp_hi, Card *opp_lo,
		      Card *board, double *outcome, bool *p1) {
  actions->clear();
  *street = 0;
  if (line[0] == '#') return false;
  vector<string> comps;
  Split(line.c_str(), ':', false, &comps);
  // Ignore SCORE line at end of file
  if (comps[0] == "SCORE") return false;
  if (comps[0] != "STATE") {
    fprintf(stderr, "Expected log line to be headed by \"STATE\"\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }
  if (sscanf(comps[1].c_str(), "%u", hand_index) != 1) {
    fprintf(stderr, "Couldn't parse hand index\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }

  if (! ParseAllActions(comps[2], false, true, actions, street)) return false;

  vector<string> player_comps;
  Split(comps[5].c_str(), '|', false, &player_comps);
  if (player_comps.size() != 2) {
    fprintf(stderr, "Expected two players on line\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }
  if (player_comps[0] == player) {
    *p1 = false;
  } else if (player_comps[1] == player) {
    *p1 = true;
  } else {
    fprintf(stderr, "Neither player on log line is target player\n");
    fprintf(stderr, "Player 0 \"%s\"\n", player_comps[0].c_str());
    fprintf(stderr, "Player 1 \"%s\"\n", player_comps[1].c_str());
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }
  // Does card_street need to match street returned by ParseActions()?
  unsigned int card_street;
  Card p2_hi, p2_lo, p1_hi, p1_lo;
  if (! ParseCardString(comps[3], &p2_hi, &p2_lo, &p1_hi, &p1_lo, board,
			&card_street)) {
    return false;
  }
  if (*p1) {
    *our_hi = p1_hi;
    *our_lo = p1_lo;
    *opp_hi = p2_hi;
    *opp_lo = p2_lo;
  } else {
    *our_hi = p2_hi;
    *our_lo = p2_lo;
    *opp_hi = p1_hi;
    *opp_lo = p1_lo;
  }
  if (*our_hi == 0) {
    fprintf(stderr, "We expect to always have hole cards for ourself\n");
    fprintf(stderr, "%s\n", comps[3].c_str());
    return false;
  }
  vector<string> outcomes;
  Split(comps[4].c_str(), '|', false, &outcomes);
  if (outcomes.size() != 2) {
    fprintf(stderr, "Expected two outcomes on line\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }
  if (*p1) {
    if (sscanf(outcomes[1].c_str(), "%lf", outcome) != 1) {
      fprintf(stderr, "Couldn't parse outcome on line\n");
      fprintf(stderr, "%s\n", line.c_str());
      exit(-1);
    }
  } else {
    if (sscanf(outcomes[0].c_str(), "%lf", outcome) != 1) {
      fprintf(stderr, "Couldn't parse outcome on line\n");
      fprintf(stderr, "%s\n", line.c_str());
      exit(-1);
    }
  }
  return true;
}

// Like above version but is not given a player; instead returns the two
// players in the hand.
bool ParseNoLimitLine(const string &line, unsigned int *street,
		      vector< vector<Action> * > *actions,
		      Card *p2_hi, Card *p2_lo, Card *p1_hi, Card *p1_lo,
		      Card *board, double *p1_outcome, string *p2, string *p1) {
  actions->clear();
  *street = 0;
  if (line[0] == '#') return false;
  vector<string> comps;
  Split(line.c_str(), ':', false, &comps);
  // Ignore SCORE line at end of file
  if (comps[0] == "SCORE") return false;
  if (comps[0] != "STATE") {
    fprintf(stderr, "Expected log line to be headed by \"STATE\"\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }

  if (! ParseAllActions(comps[2], false, true, actions, street)) return false;

  vector<string> player_comps;
  Split(comps[5].c_str(), '|', false, &player_comps);
  if (player_comps.size() != 2) {
    fprintf(stderr, "Expected two players on line\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }
  *p2 = player_comps[0];
  *p1 = player_comps[1];

  // Does card_street need to match street returned by ParseActions()?
  unsigned int card_street;
  if (! ParseCardString(comps[3], p2_hi, p2_lo, p1_hi, p1_lo, board,
			&card_street)) {
    return false;
  }
  vector<string> outcomes;
  Split(comps[4].c_str(), '|', false, &outcomes);
  if (outcomes.size() != 2) {
    fprintf(stderr, "Expected two outcomes on line\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }
  if (sscanf(outcomes[1].c_str(), "%lf", p1_outcome) != 1) {
    fprintf(stderr, "Couldn't parse outcome on line\n");
    fprintf(stderr, "%s\n", line.c_str());
    exit(-1);
  }
  return true;
}

// Messages sent over the wire have five components (when you split the line
// using ':' as a separator):
// 1) MATCHSTATE
// 2) Position (0 or 1)
// 3) Hand number
// 4) The betting action
// 5) The cards
bool ParseMatchState(const string &match_state, bool *p1, int *hand_no,
		     string *action, Card *our_hi, Card *our_lo, Card *opp_hi,
		     Card *opp_lo, Card *board, unsigned int *street) {
  *p1 = true;
  *hand_no = 0;
  *action = "";
  *our_hi = 0;
  *our_lo = 0;
  *opp_hi = 0;
  *opp_lo = 0;
  for (int i = 0; i < 5; ++i) board[i] = 0;
  vector<string> comps;
  Split(match_state.c_str(), ':', true, &comps);
  if (comps[0] != "MATCHSTATE") {
    fprintf(stderr, "Didn't find MATCHSTATE at beginning of line\n");
    return false;
  }
  if (comps.size() != 5) {
    fprintf(stderr, "Expected 5 components of MATCHSTATE line, found %i\n",
	    (int)comps.size());
    fprintf(stderr, "%s\n", match_state.c_str());
    return false;
  }
  if (comps[1] == "0") {
    *p1 = false;
  } else if (comps[1] == "1") {
    *p1 = true;
  } else {
    fprintf(stderr, "Expected 0 or 1 as second component of match state\n");
    fprintf(stderr, "%s\n", match_state.c_str());
    return false;
  }
  if (sscanf(comps[2].c_str(), "%i", hand_no) != 1) {
    fprintf(stderr, "Couldn't parse hand number from match state\n");
    fprintf(stderr, "%s\n", match_state.c_str());
    return false;
  }
  *action = comps[3];
  Card p2_hi, p2_lo, p1_hi, p1_lo;
  if (! ParseCardString(comps[4], &p2_hi, &p2_lo, &p1_hi, &p1_lo, board,
			street)) {
    return false;
  }
  if (*p1) {
    *our_hi = p1_hi;
    *our_lo = p1_lo;
    *opp_hi = p2_hi;
    *opp_lo = p2_lo;
  } else {
    *our_hi = p2_hi;
    *our_lo = p2_lo;
    *opp_hi = p1_hi;
    *opp_lo = p1_lo;
  }
  if (*our_hi == 0) {
    fprintf(stderr, "We expect to always have hole cards for ourself\n");
    fprintf(stderr, "%s\n", comps[4].c_str());
    return false;
  }
  return true;
}
