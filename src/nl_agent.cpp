// The NLAgent class implements the logic for playing poker based on
// probabilities computed by some algorithm like CFR.  It handles mapping
// the cards to buckets, looking up the probabilities, selecting an action
// according to the probabilities, flooring and translation.
//
// The NLAgent class doesn't know about networking or the ACPC protocol.  That
// logic is elsewhere.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "acpc_protocol.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_values.h"
#include "constants.h"
#include "fast_hash.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "nearest_neighbors.h"
#include "nl_agent.h"
#include "rand.h"
#include "runtime_config.h"

using namespace std;

// The last node in path should be the node after the selected bet succ.
void NLAgent::Recurse(const vector<Node *> *path, unsigned int path_index,
		      Node *parent, Node *alt_node, unsigned int pa,
		      bool smaller, bool changed,
		      vector<Node *> *alternative_bet_nodes) {
  if (path_index == path->size() - 1) {
    if (changed) {
      // Make sure we didn't already add alt_node to the vector.
      // I think we always will have when prior_alternatives_ is true, and
      // never will have otherwise.  But just to be safe, we check.
      unsigned int num = alternative_bet_nodes->size();
      if (num == 0 || (*alternative_bet_nodes)[num-1] != alt_node) {
	alternative_bet_nodes->push_back(alt_node);
      }
    }
    return;
  } else {
    Node *cur = (*path)[path_index];
    Node *next = (*path)[path_index + 1];
    unsigned int num_succs = cur->NumSuccs();
    unsigned int s;
    for (s = 0; s < num_succs; ++s) {
      Node *n = cur->IthSucc(s);
      if (n == next) break;
    }
    if (s == num_succs) {
      fprintf(stderr, "Couldn't follow path\n");
      if (exit_on_error_) exit(-1);
      return;
    }
    if (s == cur->CallSuccIndex()) {
      Node *call = alt_node->IthSucc(alt_node->CallSuccIndex());
      Recurse(path, path_index + 1, alt_node, call, pa, smaller, changed,
	      alternative_bet_nodes);
    } else if (s == cur->FoldSuccIndex()) {
      fprintf(stderr, "Shouldn't see fold succ on path\n");
      if (exit_on_error_) exit(-1);
      return;
    } else {
      Node *bet = cur->IthSucc(s);
      unsigned int bet_size = bet->LastBetTo() - cur->LastBetTo();
      double bet_frac = bet_size / (double)(2 * cur->LastBetTo());
      unsigned int alt_num_succs = alt_node->NumSuccs();
      for (unsigned int s1 = 0; s1 < alt_num_succs; ++s1) {
	if (s1 == alt_node->CallSuccIndex() ||
	    s1 == alt_node->FoldSuccIndex()) {
	  continue;
	}
	Node *alt_bet = alt_node->IthSucc(s1);
	unsigned int alt_bet_size =
	  alt_bet->LastBetTo() - alt_node->LastBetTo();
	double alt_bet_frac =
	  alt_bet_size / (double)(2 * alt_node->LastBetTo());
	if (smaller) {
	  // Alternative bet size must be same size as actual bet size or
	  // smaller.  Subtract 0.000001 for numerical stability reasons.
	  if (alt_bet_frac - 0.000001 > bet_frac) continue;
	} else {
	  // Alternative bet size must be same size as actual bet size or
	  // larger.  Add 0.000001 for numerical stability reasons.
	  if (alt_bet_frac + 0.000001 < bet_frac) continue;
	}
	bool now_changed = fabs(alt_bet_frac - bet_frac) > 0.000001;
	bool new_changed = (changed || now_changed);
	if (new_changed && prior_alternatives_ &&
	    pa != alt_node->PlayerActing()) {
	  alternative_bet_nodes->push_back(alt_bet);
	}
	Recurse(path, path_index + 1, alt_node, alt_bet, pa, smaller,
		new_changed, alternative_bet_nodes);
      }
    }
  }
}

void NLAgent::GetAlternativeBetNodes(const vector<Node *> *path,
				     bool p1, bool smaller,
				     vector<Node *> *alternative_bet_nodes) {
  alternative_bet_nodes->clear();
  BettingTree *tree;
  if (p1) tree = p1_tree_;
  else    tree = p0_tree_;
  Recurse(path, 0, NULL, tree->Root(), p1, smaller, false,
	  alternative_bet_nodes);
}

// In order to do translation, find the two succs that most closely match
// the current action.
void NLAgent::GetTwoClosestSuccs(Node *node, unsigned int actual_bet_to,
				 unsigned int *below_succ,
				 unsigned int *below_bet_to,
				 unsigned int *above_succ,
				 unsigned int *above_bet_to) {
  unsigned int num_succs = node->NumSuccs();
  // Want to find closest bet below and closest bet above
  unsigned int csi = node->CallSuccIndex();
  unsigned int fsi = node->FoldSuccIndex();
  if (debug_) {
    fprintf(stderr, "s %i pa %u nt %i ns %i fsi %i csi %i\n", node->Street(),
	    node->PlayerActing(), node->NonterminalID(), node->NumSuccs(),
	    fsi, csi);
  }
  *below_succ = kMaxUInt;
  *below_bet_to = kMaxUInt;
  *above_succ = kMaxUInt;
  *above_bet_to = kMaxUInt;
  unsigned int best_below_diff = kMaxUInt;
  unsigned int best_above_diff = kMaxUInt;
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (s == fsi) continue;
    unsigned int this_bet_to = node->IthSucc(s)->LastBetTo() * small_blind_;
    int diff = (int)this_bet_to - ((int)actual_bet_to);
    if (debug_) {
      fprintf(stderr, "s %i this_bet_to %u actual_bet_to %u diff %i\n", s,
	      this_bet_to, actual_bet_to, diff);
    }
    if (diff <= 0) {
      if (((unsigned int)-diff) < best_below_diff) {
	best_below_diff = -diff;
	*below_succ = s;
	*below_bet_to = this_bet_to;
      }
    } else {
      if (((unsigned int)diff) < best_above_diff) {
	best_above_diff = diff;
	*above_succ = s;
	*above_bet_to = this_bet_to;
      }
    }
  }
  if (debug_) {
    fprintf(stderr, "Best below %i diff %i\n", *below_succ, best_below_diff);
    fprintf(stderr, "Best above %i diff %i\n", *above_succ, best_above_diff);
  }
}

double NLAgent::BelowProb(unsigned int actual_bet_to,
			  unsigned int below_bet_to,
			  unsigned int above_bet_to,
			  unsigned int actual_pot_size) {
  double below_prob;
  if (translation_method_ == 0) {
    // Buggy.  Uses bet-to amounts, but should use bet-size amounts (expressed
    // as fractions of the pot).
    below_prob =
      (((double)(above_bet_to - actual_bet_to)) *
       ((double)(1 + below_bet_to))) /
      (((double)(above_bet_to - below_bet_to)) *
       ((double)(1 + actual_bet_to)));
  } else if (translation_method_ == 1) {
    double span = above_bet_to - below_bet_to;
    below_prob = 1.0 - (actual_bet_to - below_bet_to) / span;
    if (below_prob < 0 || below_prob > 1.0) {
      fprintf(stderr, "OOB below prob %i %i %i\n", actual_bet_to,
	      below_bet_to, above_bet_to);
      if (exit_on_error_) {
	exit(-1);
      } else if (below_prob < 0) {
	below_prob = 0;
      } else {
	below_prob = 1.0;
      }
    }
  } else if (translation_method_ == 2 || translation_method_ == 3) {
    // Express bet sizes as fraction of pot
    unsigned int last_bet_to = actual_pot_size / 2;
    int actual_bet = ((int)actual_bet_to) - ((int)last_bet_to);
    double d_actual_pot_size = actual_pot_size;
    double actual_frac = actual_bet / d_actual_pot_size;
    // below_bet could be negative, I think, so I make all these bet_to
    // quantities signed integers.
    int below_bet = ((int)below_bet_to) - ((int)last_bet_to);
    double below_frac = below_bet / d_actual_pot_size;
    if (below_frac < -1.0) {
      fprintf(stderr, "below_frac %f\n", below_frac);
      if (exit_on_error_) exit(-1);
      return 0;
    }
    int above_bet = ((int)above_bet_to) - ((int)last_bet_to);
    double above_frac = above_bet / d_actual_pot_size;
    below_prob =
      ((above_frac - actual_frac) *
       (1.0 + below_frac)) /
      ((above_frac - below_frac) *
       (1.0 + actual_frac));
    if (debug_) fprintf(stderr, "Raw below prob: %f\n", below_prob);
    if (translation_method_ == 3) {
      // Translate to nearest
      if (below_prob < 0.5) below_prob = 0;
      else                  below_prob = 1.0;
    }
  } else {
    fprintf(stderr, "Unknown translation method %i\n", translation_method_);
    if (exit_on_error_) exit(-1);
    else                below_prob = 0.5;
  }
  return below_prob;
}

// If opp has made a bet that we are treating as an all-in (but which is
// actually less than all-in) then instead of just calling, we should reraise
// all-in.
bool NLAgent::StatelessForceAllIn(Node *last_node,
				  unsigned int actual_opp_bet_to) {
  // Only applies after an opponent bet
  if (actual_opp_bet_to == 0) return false;

#if 0
  // What was the point of this test?

  // Check if there is a call with non-zero probability.
  if (! (response_actions[1] == 1 && probs[1] > 0)) {
    return false;
  }
#endif
  Node *call_node = last_node->IthSucc(last_node->CallSuccIndex());

  // Did we map opponent's bet to an all-in?
  if (call_node->LastBetTo() * small_blind_ != stack_size_) {
    return false;
  }
    // Is his actual bet less than an all-in?
  if (actual_opp_bet_to == stack_size_) {
    return false;
  }

  if (last_node->Street() < 3) {
    if (debug_) fprintf(stderr, "Forcing all in\n");
    return true;
  } else {
    // We'll skip all that logic from below
    if (debug_) fprintf(stderr, "Not forcing all in\n");
    return false;
  }
}

static bool StreetInitial(Node *node) {
  return (node->IthSucc(node->CallSuccIndex())->Street() == node->Street());
}

// Interpret a previous action by ourselves.  Find the next node and stick it
// on the end of the path.
// Sometimes we will translate an opponent's bet to all-in and then raise
// all-in just to make life simpler.  How do I recognize these situations?
// We have only two succs, call and fold.  The pot size of the call is all in.
// Tricky case.  Our bet could have been adjusted because it was illegally
// small (perhaps it was less than a big blind, or less than previous raise
// size).  We need to account for that in interpreting the previous bet.
// This raises a problem in that multiple previous bets could be rounded up
// to the actual bet size that we made.  I will have no way to tell which
// bet size I actually made!
void NLAgent::Interpret(Action a, vector<Node *> *path,
			Node *sob_node, unsigned int last_bet_to,
			unsigned int opp_bet_amount,
			bool *forced_all_in) {
  *forced_all_in = false;
  Node *node = (*path)[path->size() - 1];
  int csi = node->CallSuccIndex();
  int fsi = node->FoldSuccIndex();
  if (a.action_type == CALL || a.action_type == FOLD) {
    if (debug_) {
      fprintf(stderr, "We %s\n", a.action_type == CALL ? "call" : "fold");
    }
    // If sob_node is not NULL, we previously mapped a small bet down to
    // a check or call.  Now we are faced with our call.  There's two
    // possibilities.  If the action was /b1 then we mapped the small bet
    // down to a check, then we need to interpret our call in the usual
    // way to get to /cc in the abstraction.  At /cb1 if we mapped the small
    // bet down to a check behind, then we are *already* at /cc, and we
    // shouldn't try to follow another succ.
    if (! (sob_node && (node->Terminal() || StreetInitial(node)))) {
      int s;
      if (a.action_type == CALL) s = csi;
      else                       s = fsi;
      Node *node2 = node->IthSucc(s);
      path->push_back(node2);
    } else {
      if (debug_) {
	fprintf(stderr, "Interpret: Ignored call action\n");
      }
    }
  } else {
    if (sob_node) {
      // If sob_node is not NULL, that means the opponent's previous action
      // was a small bet that we translated between a call and a bet.
      // We got the raise probabilities from the bet.  But we put the call
      // succ on the path.  If we chose to raise, then I must replace the call
      // succ with the bet succ.
      if (debug_) {
	fprintf(stderr, "SOB: I see raise; changing call to bet for prior "
		"action; path size %i\n", (int)path->size());
      }
      (*path)[path->size() - 1] = sob_node;
      node = sob_node;
      csi = node->CallSuccIndex();
      fsi = node->FoldSuccIndex();
    }
    int actual_bet_to = a.bet_to;
    int num_succs = node->NumSuccs();
    for (int s = 0; s < num_succs; ++s) {
      if (s == fsi) continue;
      if (s == csi) continue;
      Node *bet = node->IthSucc(s);
      int this_bet_to = bet->LastBetTo() * small_blind_;
      int this_bet_amount = this_bet_to - last_bet_to;
      if (debug_) {
	fprintf(stderr, "s %i bet to %i bet amount %i\n", s, this_bet_to,
		this_bet_amount);
      }
      if (this_bet_amount < (int)(2 * small_blind_)) {
	this_bet_amount = 2 * small_blind_;
	if (debug_) {
	  fprintf(stderr, "Interpret: rounded up to %i\n", this_bet_amount);
	}
      }
      if (this_bet_amount < (int)opp_bet_amount) {
	this_bet_amount = opp_bet_amount;
	if (debug_) {
	  fprintf(stderr, "Interpret: rounded up to %u\n", this_bet_amount);
	}
      }
      // Update this_bet_to with results of two possible changes above
      this_bet_to = last_bet_to + this_bet_amount;
      // ... but make sure we don't go more than all-in
      if (this_bet_to > (int)stack_size_) this_bet_to = stack_size_;
      if (this_bet_to == actual_bet_to) {
	Node *node2 = node->IthSucc(s);
	path->push_back(node2);
	return;
      }
    }
    // Special case here.  We might have translated the opponent's bet up to
    // an all-in and reraised all-in rather than calling.
    if (num_succs == 2 &&
	node->LastBetTo() * small_blind_ == 2 * stack_size_) {
      fprintf(stderr, "All-in situation: mapping our raise down to a call\n");
      Node *node2 = node->IthSucc(csi);
      path->push_back(node2);
      *forced_all_in = true;
      return;
    }
    fprintf(stderr, "Couldn't interpret our bet of %u; path size %i\n",
	    actual_bet_to, (int)path->size());
    if (exit_on_error_) exit(-1);
  }
}

// Perform translation (if necessary) on the opponent's new action.
// Generate our response (if any).
// Can be called multiple times if there are multiple new opponent actions to
// process.  There can't be multiple new bets though.
void NLAgent::Translate(Action a, vector<Node *> *path,
			Node **sob_node, unsigned int actual_pot_size,
			unsigned int hand_index) {
  *sob_node = NULL;
  Node *node = (*path)[path->size() - 1];
  if (debug_) {
    fprintf(stderr, "Translate action type %i\n", a.action_type);
    fprintf(stderr, "Node %i\n", node->NonterminalID());
  }

  if (a.action_type == CALL || a.action_type == FOLD) {
    if (debug_) {
      fprintf(stderr, "Call or fold\n");
    }
    int s;
    if (a.action_type == CALL) s = node->CallSuccIndex();
    else                       s = node->FoldSuccIndex();
    Node *node2 = node->IthSucc(s);
    path->push_back(node2);
    if (debug_) {
      fprintf(stderr, "Node2 %i %i\n", node2->NonterminalID(),
	      node2->TerminalID());
    }
  } else {
    // We are facing a bet by the opponent.  Need to perform translation.
    // The pot size before the opponent's bet
    unsigned int actual_opp_bet_to = a.bet_to;
    unsigned int below_bet_to, above_bet_to;
    unsigned int below_succ, above_succ;
    // Find the two closest "bets" in our abstraction.  One will have a
    // bet size <= the actual bet size; one will have a bet size >= the
    // actual bet size.  We treat a check/call as a bet of size zero.
    // In some cases, there are not two possible succs in which case either
    // below_succ or above_succ will be kMaxUInt.  In some cases there will not
    // be any possible bets in which case the below succ will be the call
    // succ and the above succ will be kMaxUInt.
    GetTwoClosestSuccs(node, actual_opp_bet_to, &below_succ, &below_bet_to,
		       &above_succ, &above_bet_to);
    unsigned int call_succ = node->CallSuccIndex();
    // Special case if we are facing a bet that is smaller than the smallest
    // bet in our abstraction.  The below succ here is actually a check
    // or a call in the abstraction.
    //
    // The code here appears to allow for the possibility that the above
    // succ may be the call succ.  How could that be?  I think that can
    // occur when actual_bet_to is less than the current within-abstraction
    // pot size.  This can occur when a prior small bet is mapped up to a
    // larger bet in the abstraction.
    //
    // We shouldn't blend two actions in this situation.  We should use the
    // single closest succ.  If translate_bet_to_call is true, we should use
    // the call succ.
    if (below_succ == call_succ || above_succ == call_succ) {
      if (debug_) {
	fprintf(stderr, "Of two closest succs, one is check/call\n");
      }
      unsigned int smallest_bet_succ = kMaxUInt;
      if (below_succ == call_succ) {
	if (above_succ != kMaxUInt) {
	  smallest_bet_succ = above_succ;
	}
      } else {
	// Some corner cases here.
	unsigned int num_succs = node->NumSuccs();
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == call_succ) continue;
	  if (s == node->FoldSuccIndex()) continue;
	  smallest_bet_succ = s;
	  break;
	}
	if (smallest_bet_succ == kMaxUInt) {
	  fprintf(stderr, "No smallest bet succ?!?\n");
	  fprintf(stderr, "below_succ %u above_succ %u call_succ %u\n",
		  below_succ, above_succ, call_succ);
	  fprintf(stderr, "st %i pa %u nt %i\n", node->Street(),
		  node->PlayerActing(), node->NonterminalID());
	  if (exit_on_error_) exit(-1);
	  else                return;
	}
      }
      Node *smallest_bet_node = NULL;
      // When can smallest_bet_succ be unset?
      if (smallest_bet_succ == kMaxUInt) {
	fprintf(stderr, "smallest_bet_succ unset?!?\n");
	fprintf(stderr, "below_succ %i above_succ %i call_succ %i\n",
		below_succ, above_succ, call_succ);
	fprintf(stderr, "st %u p%u nt %i\n", node->Street(),
		node->PlayerActing(), node->NonterminalID());
	fprintf(stderr, "action type %i bet-to %i\n", a.action_type,
		a.bet_to);
	if (exit_on_error_) exit(-1);
	else                return;
      }
      smallest_bet_node = node->IthSucc(smallest_bet_succ);
      if (translate_bet_to_call_ && ! translate_to_larger_) {
	unsigned int selected_succ;
	// Sometimes we have only one valid succ.
	if (above_succ == kMaxUInt) {
	  selected_succ = below_succ;
	} else if (below_succ == kMaxUInt) {
	  selected_succ = above_succ;
	} else {
	  // Opponent's bet size is between two bets in our abstraction.
	  double below_prob = BelowProb(actual_opp_bet_to, below_bet_to,
					above_bet_to, actual_pot_size);
	  // See comment below
	  unsigned int seed = hand_index * 967 +
	    (node->PlayerActing() == 1 ? 23 : 0) + path->size() * 7;
	  SeedRand(seed);
	  double r = RandZeroToOne();
	  if (debug_) {
	    fprintf(stderr, "hand_index %u pa %u path sz %i seed %u r %f\n",
		    hand_index, node->PlayerActing(), (int)path->size(),
		    seed, r);
	  }
	  if (r < below_prob) {
	    if (debug_) {
	      fprintf(stderr, "Selected below bet; succ %i\n", below_succ);
	    }
	    selected_succ = below_succ;
	  } else {
	    if (debug_) {
	      fprintf(stderr, "Selected above bet; succ %i\n", above_succ);
	    }
	    selected_succ = above_succ;
	  }
	}
	if (debug_) fprintf(stderr, "selected_succ %i\n", selected_succ);
	if (selected_succ == call_succ) {
	  Node *call_node = node->IthSucc(call_succ);
	  path->push_back(call_node);
	  // Don't use above_succ; it might be kMaxUInt
	  *sob_node = smallest_bet_node;
	  if (debug_) fprintf(stderr, "sob_node is succ %u\n",
			      smallest_bet_succ);
	} else {
	  Node *bet_node = node->IthSucc(selected_succ);
	  path->push_back(bet_node);
	}
      } else {
	if (debug_) fprintf(stderr, "Mapping to smallest bet succ %u\n",
			    smallest_bet_succ);
	// Map this bet to the smallest bet in our abstraction
	path->push_back(smallest_bet_node);
      }
    } else {
      unsigned int selected_succ;
      if (above_succ == kMaxUInt) {
	// Can happen if we do not have all-ins in our betting abstraction
	selected_succ = below_succ;
	if (debug_) {
	  fprintf(stderr, "Selected below bet; succ %i\n", below_succ);
	}
      } else if (below_succ == kMaxUInt) {
	// There should always be a below succ.  All abstractions always
	// allow check and call.
	fprintf(stderr, "No below succ?!?\n");
	if (exit_on_error_) exit(-1);
	selected_succ = above_succ;
	if (debug_) {
	  fprintf(stderr, "Selected above bet (only); succ %i\n", above_succ);
	}
      } else {
	if (translate_to_larger_) {
	  selected_succ = above_succ;
	} else {
	  // Opponent's bet size is between two bets in our abstraction.
	  double below_prob = BelowProb(actual_opp_bet_to, below_bet_to,
					above_bet_to, actual_pot_size);
	  if (debug_) fprintf(stderr, "below_prob %f\n", below_prob);
	  // I need to make sure we translate a given action the same way
	  // each time we get here.  But also don't want my translation to
	  // be deterministic preflop.  So seed the RNG to something based on
	  // the hand index and the action index.
	  unsigned int seed = hand_index * 967 +
	    (node->PlayerActing() == 1 ? 23 : 0) + path->size() * 7;
	  SeedRand(seed);
	  double r = RandZeroToOne();
	  if (r < below_prob) {
	    if (debug_) {
	      fprintf(stderr, "Selected below bet; succ %i\n", below_succ);
	    }
	    selected_succ = below_succ;
	  } else {
	    if (debug_) {
	      fprintf(stderr, "Selected above bet; succ %i\n", above_succ);
	    }
	    selected_succ = above_succ;
	  }
	}
      }
      Node *bet_node = node->IthSucc(selected_succ);
      path->push_back(bet_node);
    }
  }
}

void NLAgent::UpdateCards(int street, Card our_hi, Card our_lo,
			  Card *raw_board, unsigned int ***current_buckets) {
  if (debug_) fprintf(stderr, "UpdateCards %i\n", street);
  for (unsigned int st = 0; st <= (unsigned int)street; ++st) {
    if (st == 0) {
      Card cards[2];
      cards[0] = our_hi;
      cards[1] = our_lo;
      unsigned int hcp = HCPIndex(0, cards);
      current_buckets[1][0][0] = p1_buckets_->Bucket(0, hcp);
      if (p0_buckets_ == p1_buckets_) {
	current_buckets[0][0][0] = current_buckets[1][0][0];
      } else {
	current_buckets[0][0][0] = p0_buckets_->Bucket(0, hcp);
      }
    } else {
      unsigned int num_hole_cards = Game::NumCardsForStreet(0);
      unsigned int num_board_cards = Game::NumBoardCards(st);
      Card raw_board[5], raw_hole_cards[2], canon_board[5];
      Card canon_hole_cards[2];
      raw_hole_cards[0] = our_hi;
      raw_hole_cards[1] = our_lo;
      CanonicalizeCards(raw_board, raw_hole_cards, st, canon_board,
			canon_hole_cards);
      unsigned int bd = BoardTree::LookupBoard(canon_board, st);
      // Put the hole cards at the beginning
      Card canon_cards[7];
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	canon_cards[i] = canon_hole_cards[i];
      }
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	canon_cards[num_hole_cards + i] = canon_cards[i];
      }
      unsigned int hcp = HCPIndex(st, canon_cards);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int h = bd * num_hole_card_pairs + hcp;
      current_buckets[1][st][0] = p1_buckets_->Bucket(st, h);
      if (p0_buckets_ == p1_buckets_) {
	current_buckets[0][st][0] = current_buckets[1][st][0];
      } else {
	current_buckets[0][st][0] = p0_buckets_->Bucket(st, h);
      }
    }
  }
}

NLAgent::NLAgent(const CardAbstraction *p0_ca, const CardAbstraction *p1_ca, 
		 const BettingAbstraction *ba, const CFRConfig *cc,
		 const RuntimeConfig *p0_rc, const RuntimeConfig *p1_rc, 
		 BettingTree *p0_tree, BettingTree *p1_tree, bool debug,
		 bool exit_on_error, unsigned int small_blind,
		 unsigned int stack_size) {
  p0_tree_ = p0_tree;
  p1_tree_ = p1_tree;
  debug_ = debug;
  exit_on_error_ = exit_on_error;
  small_blind_ = small_blind;
  stack_size_ = stack_size;
  // Assume these are shared by P0 and P1
  respect_pot_frac_ = p1_rc->RespectPotFrac();
  no_small_bets_ = p1_rc->NoSmallBets();
  translation_method_ = p1_rc->TranslationMethod();
  hard_coded_root_strategy_ = p1_rc->HardCodedRootStrategy();
  hard_coded_r200_strategy_ = p1_rc->HardCodedR200Strategy();
  hard_coded_r250_strategy_ = p1_rc->HardCodedR250Strategy();
  hard_coded_r200r600_strategy_ = p1_rc->HardCodedR200R600Strategy();
  hard_coded_r200r800_strategy_ = p1_rc->HardCodedR200R800Strategy();
  unsigned int max_street = Game::MaxStreet();
  BoardTree::Create();
  BoardTree::CreateLookup();

  const unsigned int *p0_num_buckets, *p1_num_buckets;
  if (p0_ca == p1_ca) {
    p1_buckets_ = new Buckets(*p1_ca, false);
    p0_buckets_ = p1_buckets_;
    p1_num_buckets = p1_buckets_->NumBuckets();
    p0_num_buckets = p1_num_buckets;
  } else {
    p1_buckets_ = new Buckets(*p1_ca, false);
    p0_buckets_ = new Buckets(*p0_ca, false);
    p1_num_buckets = p1_buckets_->NumBuckets();
    p0_num_buckets = p0_buckets_->NumBuckets();
  }
#if 0
  bool *in_memory = new bool[max_street + 1];
  for (unsigned int s = 0; s <= max_street; ++s) in_memory[s] = false;
  // Assume these are shared by P0 and P1
  const vector<unsigned int> &v = p1_rc->BucketMemoryStreets();
  unsigned int num = v.size();
  for (unsigned int i = 0; i < num; ++i) {
    in_memory[v[i]] = true;
  }
  // Buckets now always in memory.  Need to change.
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (in_memory[st]) {
      p1_buckets_instance_->Initialize(st, MEMORY, p1_cai);
      if (p1_buckets_instance_ != p2_buckets_instance_) {
	p2_buckets_instance_->Initialize(st, MEMORY, p2_cai);
      }
    } else {
      p1_buckets_instance_->Initialize(st, DISK, p1_cai);
      if (p1_buckets_instance_ != p2_buckets_instance_) {
	p2_buckets_instance_->Initialize(st, DISK, p2_cai);
      }
    }
  }
  delete [] in_memory;
#endif

  unique_ptr<bool []> players(new bool[2]);
  probs_ = new CFRValues *[2];
  players[0] = true;
  players[1] = false;
  probs_[0] = new CFRValues(players.get(), true, nullptr, p1_tree_,
			    0, 0, *p1_ca, *p1_buckets_, nullptr);
  players[0] = true;
  players[1] = false;
  probs_[0] = new CFRValues(players.get(), true, nullptr, p0_tree_,
			    0, 0, *p0_ca, *p0_buckets_, nullptr);

  fold_to_alternative_streets_ = new bool[max_street + 1];
  call_alternative_streets_ = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    fold_to_alternative_streets_[st] = false;
    call_alternative_streets_[st] = false;
  }
  const vector<unsigned int> &ftav = p1_rc->FoldToAlternativeStreets();
  unsigned int fta_num = ftav.size();
  for (unsigned int i = 0; i < fta_num; ++i) {
    fold_to_alternative_streets_[ftav[i]] = true;
  }
  const vector<unsigned int> &cav = p1_rc->CallAlternativeStreets();
  unsigned int ca_num = cav.size();
  for (unsigned int i = 0; i < ca_num; ++i) {
    if (cav[i] != max_street) {
      fprintf(stderr, "Only max street can be a CallAlternativeStreet\n");
      exit(-1);
    }
    call_alternative_streets_[cav[i]] = true;
  }
  ftann_ = p1_rc->FTANN();
  eval_overrides_ = p1_rc->EvalOverrides();
  override_min_pot_size_ = p1_rc->OverrideMinPotSize();
  min_neighbor_folds_ = p1_rc->MinNeighborFolds();
  if (min_neighbor_folds_ == 0) min_neighbor_folds_ = 1;
  min_neighbor_frac_ = p1_rc->MinNeighborFrac();
  min_alternative_folds_ = p1_rc->MinAlternativeFolds();
  if (min_alternative_folds_ == 0) min_alternative_folds_ = 1;
  min_actual_alternative_folds_ = p1_rc->MinActualAlternativeFolds();
  if (min_actual_alternative_folds_ == 0) min_actual_alternative_folds_ = 1;
  min_frac_alternative_folded_ = p1_rc->MinFracAlternativeFolded();
  prior_alternatives_ = p1_rc->PriorAlternatives();
  translate_to_larger_ = p1_rc->TranslateToLarger();
  translate_bet_to_call_ = p1_rc->TranslateBetToCall();

  if (p1_rc->NearestNeighbors()) {
    nearest_neighbors_ = new NearestNeighbors *[2];
    nearest_neighbors_[0] = new NearestNeighbors(p0_ca, p0_rc,
						 p0_num_buckets);
    nearest_neighbors_[1] = new NearestNeighbors(p1_ca, p1_rc,
						 p1_num_buckets);
  } else {
    nearest_neighbors_ = NULL;
  }
}

NLAgent::~NLAgent(void) {
  if (nearest_neighbors_) {
    delete nearest_neighbors_[0];
    delete nearest_neighbors_[1];
    delete [] nearest_neighbors_;
  }
  delete [] fold_to_alternative_streets_;
  delete [] call_alternative_streets_;
  for (unsigned int p1 = 0; p1 <= 1; ++p1) {
    delete probs_[p1];
  }
  delete [] probs_;
  if (p1_buckets_ != p0_buckets_) {
    delete p0_buckets_;
  }
  delete p1_buckets_;
  // Don't delete trees; we don't own them
}

float *NLAgent::CurrentProbs(Node *node, unsigned int b) {
  // Arbitrary response if node is NULL.  Does this ever happen?
  if (node == NULL) {
    fprintf(stderr, "NLAgent::CurrentProbs() NULL node?!?\n");
    if (exit_on_error_) exit(-1);
    else                return NULL;
  }

  unsigned int num_succs = node->NumSuccs();
  unsigned int dsi = node->DefaultSuccIndex();
  unsigned int street = node->Street();
  unsigned int pa = node->PlayerActing();
  if (b == kMaxUInt) {
    fprintf(stderr, "Uninitialized bucket node %i street %i p%u\n",
	    node->NonterminalID(), street, pa);
    time_t rawtime;
    time(&rawtime);
    struct tm timeinfo;
    localtime_r(&rawtime, &timeinfo);
    char tbuf[100];
    asctime_r(&timeinfo, tbuf);
    tbuf[strlen(tbuf) - 1] = 0;
    fprintf(stderr, "Timestamp: %s\n", tbuf);
    if (exit_on_error_) exit(-1);
    return NULL;
  }

  if (debug_) {
    fprintf(stderr, "Node %i (%i) street %i b %i\n", node->NonterminalID(),
	    node->TerminalID(), street, b);
  }

  unsigned int offset = b * num_succs;
  float *probs = new float[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    probs[s] = probs_[pa]->Prob(pa, street, node->NonterminalID(), offset, s,
				num_succs, dsi);
  }
  return probs;
}

// Check if we are all-in based on the actions in the current match state.
// We are all-in if the final actions are one or more calls preceded by
// a bet whose bet-to amount is the stack size.
bool NLAgent::AreWeAllIn(vector<Action> *actions) {
  unsigned int num_actions = actions->size();
  if (num_actions < 2) return false;
  Action last_action = (*actions)[num_actions - 1];
  if (last_action.action_type != CALL) return false;
  int i = num_actions - 2;
  while (i >= 0) {
    Action a = (*actions)[i];
    if (a.action_type == CALL) {
      --i;
      continue;
    }
    // Can't happen, but whatever
    if (a.action_type != BET) return false;
    if (a.bet_to == stack_size_) return true;
    else                         return false;
  }
  return false;
}

void NLAgent::WhoseAction(vector<Action> *actions, bool *p0_to_act,
			  bool *p1_to_act) {
  unsigned int street = 0;
  bool my_p1_to_act = true;
  // This includes P1's first action preflop even though there is in some
  // sense a bet pending (the big blind).
  bool first_action_on_street = true;
  unsigned int num_actions = actions->size();
  for (unsigned int i = 0; i < num_actions; ++i) {
    Action a = (*actions)[i];
    if (a.action_type == BET) {
      my_p1_to_act = ! my_p1_to_act;
      first_action_on_street = false;
    } else if (a.action_type == FOLD) {
      *p0_to_act = false;
      *p1_to_act = false;
      return;
    } else {
      // Check/call
      if (! first_action_on_street) {
	if (street == 3) {
	  // Showdown
	  *p0_to_act = false;
	  *p1_to_act = false;
	  return;
	} else {
	  // Advance to next street
	  ++street;
	  first_action_on_street = true;
	  my_p1_to_act = false;
	}
      } else {
	// Open check, or open-call preflop
	my_p1_to_act = ! my_p1_to_act;
	first_action_on_street = false;
      }
    }
  }
  *p0_to_act = ! my_p1_to_act;
  *p1_to_act = my_p1_to_act;
}

// Assumption is that if last action is a bet then it is a bet by the
// opponent.
bool NLAgent::AreWeFacingBet(const vector<Action> *actions,
			     unsigned int *opp_bet_to,
			     unsigned int *opp_bet_amount) {
  *opp_bet_to = 0;
  *opp_bet_amount = 0;
  unsigned int num_actions = actions->size();
  if (num_actions == 0) {
    return false;
  } else {
    unsigned int last_bet_to = 2 * small_blind_;
    for (unsigned int i = 0; i < num_actions - 1; ++i) {
      Action a = (*actions)[i];
      if (a.action_type == BET) last_bet_to = a.bet_to;
    }
    Action last_action = (*actions)[num_actions - 1];
    if (last_action.action_type == BET) {
      *opp_bet_to = last_action.bet_to;
      *opp_bet_amount = *opp_bet_to - last_bet_to;
      return true;
    } else {
      return false;
    }
  }
}

// We keep track of p1_to_act, the current street and whether we are
// street-initial.  We do *not* rely on the node in the tree to give us this
// information.  When we translate a bet to a call the node in the path may
// not accurately reflect whose action it really is.
void NLAgent::ProcessActions(vector<Action> *actions, unsigned int pa,
			     unsigned int hand_index,
			     vector<Node *> *path, Node **sob_node,
			     bool *terminate, unsigned int *last_bet_to) {
  *sob_node = NULL;
  *last_bet_to = 2 * small_blind_;
  Node *node = pa == 1 ? p1_tree_->Root() : p0_tree_->Root();
  path->clear();
  path->push_back(node);

  *terminate = false;
  unsigned int num_actions = actions->size();
  bool forced_all_in = false;
  unsigned int opp_bet_amount = 0;
  bool p1_to_act = true;
  unsigned int st = 0;
  bool street_initial = true;
  for (unsigned int i = 0; i < num_actions; ++i) {
    Action a = (*actions)[i];
    Node *current = (*path)[path->size() - 1];
    if (forced_all_in) {
      // This can happen when we translate an opponent's bet up into an
      // all-in and then we send back a raise rather than a call.  The next
      // opponent action may be out of sync.  This could be on the river
      // or it could be on an earlier street, but either way I think we can
      // just send back BA_NONE.
      if (current->LastBetTo() * small_blind_ != stack_size_) {
	if (debug_) {
	  fprintf(stderr, "Unexpected pot size\n");
	}
	if (exit_on_error_) exit(-1);
      }
      if (debug_) {
	fprintf(stderr, "Ignoring opponent final action after forced all-in\n");
      }
      *terminate = true;
      return;
    }
    if (p1_to_act == (pa == 1)) {
      if (debug_) {
	fprintf(stderr, "Interpreting %i/%i\n", i, num_actions);
      }
      Interpret(a, path, *sob_node, *last_bet_to, opp_bet_amount,
		&forced_all_in);
      *sob_node = NULL;
      opp_bet_amount = 0;
    } else {
      if (debug_) {
	fprintf(stderr, "Translating %i/%i\n", i, num_actions);
      }
      Translate(a, path, sob_node, 2 * *last_bet_to, hand_index);
      if (a.action_type == BET) {
	opp_bet_amount = a.bet_to - *last_bet_to;
      } else {
	opp_bet_amount = 0;
      }
    }
    if (a.action_type == BET) *last_bet_to = a.bet_to;
    if (a.action_type == CALL && ! street_initial) {
      // A (non-street-initial) call or check advances the street, leads to a
      // P2 choice, and sets street_initial to true.  (It doesn't hurt to take
      // all these steps when the call actually leads to showdown.)
      ++st;
      p1_to_act = false;
      street_initial = true;
    } else {
      // Could be a check, a bet, a limp or a fold.
      p1_to_act = ! p1_to_act;
      street_initial = false;
    }
  }
}

// This is a special case when we have an opponent bet that is smaller than
// the smallest bet in our abstraction.  At this point we are undecided as
// to whether to map this bet to a check or to a bet.  We get the raise
// probs as if responding to the opponent's smallest bet.  We then draw a
// random number and decide if we are raising or not.  If we decide to
// raise, we replace the last node in the path (which was the opponent's
// check) with the smallest opponent bet node.
bool NLAgent::HandleRaise(Node *bet_node, unsigned int hand_index,
			  unsigned int ***current_buckets,
			  vector<Node *> *path) {
  unsigned int num_succs = bet_node->NumSuccs();
  unsigned int fsi = bet_node->FoldSuccIndex();
  unsigned int csi = bet_node->CallSuccIndex();
  unsigned int st = bet_node->Street();
  unsigned int b = current_buckets[bet_node->PlayerActing()][st][0];
  float *raw_probs = CurrentProbs(bet_node, b);
  double sum_raise_probs = 0;
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (s != fsi && s != csi) {
      sum_raise_probs += raw_probs[s];
    }
  }
  if (debug_) {
    fprintf(stderr, "HandleRaise: sum_raise_probs %f\n", sum_raise_probs);
  }
  delete [] raw_probs;
  // I seed the RNG to something based on the hand index and the action index.
  // So behaviour should be replicable, but only for the same hand and same
  // action.
  unsigned int seed = hand_index * 1971 + path->size() * 11;
  SeedRand(seed);
  double r = RandZeroToOne();
  if (r < sum_raise_probs) {
    if (debug_) {
      fprintf(stderr, "HandRaise: decided to raise\n");
    }
    // Replace the last node in the path with the bet node.
    (*path)[path->size() - 1] = bet_node;
    return true;
  } else {
    if (debug_) {
      fprintf(stderr, "HandleRaise: decided not to raise\n");
    }
    return false;
  }
}

// actual_opp_bet_to can't be different from last_bet_to, can it?
double *NLAgent::GetActionProbs(const vector<Action> &actions,
				vector<Node *> *path, Node *sob_node,
				unsigned int hand_index,
				unsigned int ***current_buckets,
				bool p1, unsigned int *opp_bet_amount,
				bool *force_all_in, bool *force_call) {
  bool force_raise =
    (sob_node && HandleRaise(sob_node, hand_index, current_buckets, path));
  Node *current_node = (*path)[path->size() - 1];
  unsigned int num_succs = current_node->NumSuccs();
  unsigned int csi = current_node->CallSuccIndex();
  unsigned int dsi = current_node->DefaultSuccIndex();

  if (sob_node && ! force_raise) {
    // If we map a small bet down to a check/call *and* we decide not to
    // raise, we must force a call.  It wouldn't make sense to fold to a
    // size zero bet.
    // Keep in mind the current node might be a terminal (showdown) node.
    *force_call = true;
#if 0
    double *probs = new double[num_succs];
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (s == csi) probs[s] = 1.0;
      else          probs[s] = 0;
    }
#endif
    *force_all_in = false;
    if (debug_) {
      fprintf(stderr, "Not raising; forcing check/call\n");
#if 0
      fprintf(stderr, "Not raising; forcing check/call; probs:");
      for (unsigned int s = 0; s < num_succs; ++s) {
	fprintf(stderr, " %f", probs[s]);
      }
      fprintf(stderr, "\n");
#endif
    }
    // return probs;
    return NULL;
  }

  // Note that the "bet amount" is different from the "bet to" amount.
  unsigned int actual_opp_bet_to;
  bool facing_bet = AreWeFacingBet(&actions, &actual_opp_bet_to,
				   opp_bet_amount);
  if (facing_bet) {
    if (debug_) fprintf(stderr, "Opp bet amount: %i\n", *opp_bet_amount);
  }

  unsigned int fsi = current_node->FoldSuccIndex();
  unsigned int st = current_node->Street();
  unsigned int max_street = Game::MaxStreet();

  unsigned int b = current_buckets[current_node->PlayerActing()][st][0];
  float *raw_probs = CurrentProbs(current_node, b);
  if (debug_) {
    fprintf(stderr, "raw_probs:");
    for (unsigned int s = 0; s < num_succs; ++s) {
      fprintf(stderr, " %f", raw_probs[s]);
    }
    fprintf(stderr, "\n");
  }

  double orig_call_prob = 0, fold_prob = 0;
  if (current_node->HasCallSucc()) orig_call_prob = raw_probs[csi];
  if (current_node->HasFoldSucc()) fold_prob = raw_probs[fsi];
  unsigned int pot_size = current_node->LastBetTo() * 2;

  if (force_raise) {
    // Need to set call/fold probs to zero, scale up raise probs
    double sum_bet_probs = 1.0 - orig_call_prob - fold_prob;
    // We know sum_bet_probs > 0
    double scale_up = 1.0 / sum_bet_probs;
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (s == csi || s == fsi) raw_probs[s] = 0;
      else                      raw_probs[s] *= scale_up;
    }
    orig_call_prob = 0;
    fold_prob = 0;
    if (debug_) {
      fprintf(stderr, "modified raw_probs:");
      for (unsigned int s = 0; s < num_succs; ++s) {
	fprintf(stderr, " %f", raw_probs[s]);
      }
      fprintf(stderr, "\n");
    }
  }

  // Should I only try calling if original decision is to fold?  Or also
  // try calling if original decision is to raise?
  if (facing_bet && fold_prob > 0 && call_alternative_streets_[st] &&
      pot_size >= override_min_pot_size_) {
    vector<Node *> alternative_bet_nodes;
    GetAlternativeBetNodes(path, p1, false, &alternative_bet_nodes);

    vector<unsigned int> buckets;
    unsigned int b = current_buckets[p1][st][0];
    if (ftann_ && nearest_neighbors_ && st == max_street) {
      nearest_neighbors_[p1]->GetNeighbors(b, &buckets);
      buckets.push_back(b);
    } else {
      buckets.push_back(b);
    }
    unsigned int num_neighbors = buckets.size();
    double max_call_prob = orig_call_prob;
    num_alternative_bets_ = alternative_bet_nodes.size();
    unsigned int num_candidates = 0;
    for (unsigned int i = 0; i < num_alternative_bets_; ++i) {
      Node *alternative_bet_node = alternative_bet_nodes[i];
      int acsi = alternative_bet_node->CallSuccIndex();
      if (acsi >= 0) {
	unsigned int ast = alternative_bet_node->Street();
	if (debug_) {
	  fprintf(stderr, "Alternative bet node %u\n",
		  alternative_bet_node->NonterminalID());
	}
	if (ast < st) {
	  ++num_candidates;
	  unsigned int b = current_buckets[p1][ast][0];
	  float *probs2 = CurrentProbs(alternative_bet_node, b);
	  double alt_call_prob = probs2[acsi];
	  if (alt_call_prob > max_call_prob) max_call_prob = alt_call_prob;
	  if (alt_call_prob > 0) ++num_alternative_called_;
	  delete [] probs2;
	} else {
	  for (unsigned int i = 0; i < num_neighbors; ++i) {
	    ++num_candidates;
	    // nb is the bucket that may be our actual bucket or a neighboring
	    // bucket.
	    unsigned int nb = buckets[i];
	    float *probs2 = CurrentProbs(alternative_bet_node, nb);
	    double alt_call_prob = probs2[acsi];
	    if (alt_call_prob > max_call_prob) max_call_prob = alt_call_prob;
	    if (alt_call_prob > 0) ++num_alternative_called_;
	    delete [] probs2;
	  }
	}
      }
    }
    if (num_alternative_bets_ > 0) {
      frac_alternative_called_ =
	num_alternative_called_ / (double)num_candidates;
    }
    if (max_call_prob > orig_call_prob) {
      changed_fold_to_call_ = true;
      double old_other = 1.0 - orig_call_prob;
      double new_other = 1.0 - max_call_prob;
      double scale_down = new_other / old_other;
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (s != csi) raw_probs[s] *= scale_down;
      }
      raw_probs[csi] = max_call_prob;
    }
  }

  // Original code only invoked this logic if the original decision was to
  // call.  Not sure if I can mimic that exactly.  But I can at least not
  // invoke this logic if the call prob is zero.  And since we have purified
  // probs, this is a pretty good approximation.
  if (facing_bet && orig_call_prob > 0 && pot_size >= override_min_pot_size_) {
    if (nearest_neighbors_ && st == max_street && min_neighbor_folds_ < 999) {
      vector<unsigned int> v;
      unsigned int b = current_buckets[p1][max_street][0];
      nearest_neighbors_[p1]->GetNeighbors(b, &v);
      unsigned int num_neighbors = v.size();
      // Once we get up to as many as five neighbors it became too aggressive
      // to fold if *any* neighbor said to fold.
      if (num_neighbors > 0) {
	num_neighbor_folded_ = 0;
	for (unsigned int i = 0; i < num_neighbors; ++i) {
	  unsigned int nb = v[i];
	  unsigned int offset = nb * num_succs;
	  double fold_prob = probs_[p1]->Prob(p1, max_street, 
					      current_node->NonterminalID(),
					      offset, fsi, num_succs, dsi);
	  // Assume purified system
	  if (fold_prob == 1.0) ++num_neighbor_folded_;
	}
	frac_neighbor_folded_ = num_neighbor_folded_ / (double)num_neighbors;
	if (num_neighbor_folded_ >= min_neighbor_folds_ &&
	    frac_neighbor_folded_ >= min_neighbor_frac_) {
	  changed_call_to_fold_ = true;
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    if (s == fsi)      raw_probs[s] = 1.0;
	    else               raw_probs[s] = 0;
	  }
	}
      }
    }

    // Make sure we didn't already set call prob to zero in nearest
    // neighbor calculation above.
    // When evaluating overrides, do this calculation even if we have already
    // decided to call.
    if (fold_to_alternative_streets_[st] &&
	(eval_overrides_ || ! changed_call_to_fold_)) {
      vector<Node *> alternative_bet_nodes;
      GetAlternativeBetNodes(path, p1, true, &alternative_bet_nodes);
      vector<unsigned int> buckets;
      unsigned int b = current_buckets[p1][st][0];
      if (ftann_ && nearest_neighbors_ && st == max_street) {
	nearest_neighbors_[p1]->GetNeighbors(b, &buckets);
	if (debug_) {
	  printf("NN of b %u:", b);
	  unsigned int num = buckets.size();
	  for (unsigned int i = 0; i < num; ++i) {
	    printf(" %u", buckets[i]);
	  }
	  printf("\n");
	}
	buckets.push_back(b);
      } else {
	buckets.push_back(b);
      }
      unsigned int num_neighbors = buckets.size();

      num_alternative_bets_ = alternative_bet_nodes.size();
      unsigned int num_candidates = 0;
      for (unsigned int i = 0; i < num_alternative_bets_; ++i) {
	Node *alternative_bet_node = alternative_bet_nodes[i];
	unsigned int ast = alternative_bet_node->Street();
	if (debug_) {
	  fprintf(stderr, "Alternative bet node %u\n",
		  alternative_bet_node->NonterminalID());
	}
	int afsi = alternative_bet_node->FoldSuccIndex();
	if (ast < st) {
	  ++num_candidates;
	  unsigned int b = current_buckets[p1][ast][0];
	  float *probs2 = CurrentProbs(alternative_bet_node, b);
	  if (afsi >= 0) {
	    double alt_fold_prob = probs2[afsi];
	    if (alt_fold_prob > 0) {
	      ++num_alternative_folded_;
	      ++num_alternative_actual_folded_;
	    }
	  }
	  delete [] probs2;
	} else {
	  for (unsigned int i = 0; i < num_neighbors; ++i) {
	    ++num_candidates;
	    // nb is the bucket that may be our actual bucket or a neighboring
	    // bucket.
	    unsigned int nb = buckets[i];
	    float *probs2 = CurrentProbs(alternative_bet_node, nb);
	    if (afsi >= 0) {
	      double alt_fold_prob = probs2[afsi];
	      if (alt_fold_prob > 0) {
		if (debug_) {
		  fprintf(stderr, "nb %u fold\n", nb);
		}
		++num_alternative_folded_;
		if (nb == b) {
		  ++num_alternative_actual_folded_;
		}
	      }
	    }
	    delete [] probs2;
	  }
	}
      }
      if (num_alternative_bets_ > 0) {
	frac_alternative_folded_ =
	  num_alternative_folded_ / (double)num_candidates;
	if (debug_) {
	  fprintf(stderr, "faf %f (%u / %u)\n", frac_alternative_folded_,
		  num_alternative_folded_, num_candidates);
	}
      }
      // Do an OR of the first two, combined with an AND.
      if ((num_alternative_folded_ >= min_alternative_folds_ ||
	   num_alternative_actual_folded_ >= min_actual_alternative_folds_) &&
	  frac_alternative_folded_ >= min_frac_alternative_folded_) {
	changed_call_to_fold_ = true;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == fsi)      raw_probs[s] = 1.0;
	  else               raw_probs[s] = 0;
	}
      }
    }
  }

  double *probs = new double[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) probs[s] = raw_probs[s];

  delete [] raw_probs;

  *force_all_in = facing_bet && 
    StatelessForceAllIn((*path)[path->size() - 1], actual_opp_bet_to);

  if (debug_) {
    for (unsigned int s = 0; s < num_succs; ++s) {
      fprintf(stderr, "s %u prob %f\n", s, probs[s]);
    }
  }
  return probs;
}


// Now that I have a stateless interface I don't know the last hand number
// and can't tell in the same way if I have a new hand.  But maybe I don't
// need to know if I have a new hand.  Just look up the buckets each time.
// I will desupport fixed_seed_ for now.  But if I want to resupport it,
// seed based on a hash of the match state?  Or seed based on the hand number
// at every call?  See comment about synchronized actions below.
BotAction NLAgent::HandleStateChange(const string &match_state,
				     unsigned int *we_bet_to) {
  if (debug_) {
    fprintf(stderr, "----------------------------------------\n");
    fprintf(stderr, "%s\n", match_state.c_str());
  }
  *we_bet_to = 0; // Default
  bool p1;
  unsigned int hand_index;
  unsigned int board_street;
  string action_str;
  Card our_hi, our_lo, opp_hi, opp_lo;
  Card board[5];
  if (! ParseMatchState(match_state, &p1, (int *)&hand_index, &action_str,
			&our_hi, &our_lo, &opp_hi, &opp_lo, board,
			&board_street)) {
    fprintf(stderr, "Couldn't parse match state message from server:\n");
    fprintf(stderr, "  %s\n", match_state.c_str());
    if (exit_on_error_) exit(-1);
    else                return BA_CALL;
  }
  if (debug_) fprintf(stderr, "P1: %i\n", (int)p1);

#if 0
  // The hand index needs to be the same throughout the hand (so translation
  // decisions are made consistently) so this code is definitely broken.
  // For now, fixed_seed is mandatory.
  //
  // I now seed the RNG each time before a random number is needed.
  if (! fixed_seed_) {
    hand_index = RandBetween(0, 1000000);
  }
#endif

  changed_call_to_fold_ = false;
  changed_fold_to_call_ = false;
  num_alternative_folded_ = 0;
  num_alternative_actual_folded_ = 0;
  num_alternative_called_ = 0;
  frac_alternative_folded_ = 0;
  frac_alternative_called_ = 0;
  num_alternative_bets_ = 0;
  num_neighbor_folded_ = 0;
  frac_neighbor_folded_ = 0;

  unsigned int max_street = Game::MaxStreet();
  unsigned int ***current_buckets = new unsigned int **[2];
  for (unsigned int p1 = 0; p1 <= 1; ++p1) {
    current_buckets[p1] = new unsigned int *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      current_buckets[p1][st] = new unsigned int[1];
    }
  }
  UpdateCards((int)board_street, our_hi, our_lo, board, current_buckets);
  if (debug_) {
    fprintf(stderr, "Action str: %s\n", action_str.c_str());
  }
  vector<Action> actions;
  ParseActions(action_str, false, exit_on_error_, &actions);

  if (AreWeAllIn(&actions)) {
    if (debug_) {
      fprintf(stderr, "We are all-in; returning no-action\n");
    }
    *we_bet_to = 0;
    for (unsigned int p1 = 0; p1 <= 1; ++p1) {
      for (unsigned int st = 0; st <= max_street; ++st) {
	delete [] current_buckets[p1][st];
      }
      delete [] current_buckets[p1];
    }
    delete [] current_buckets;
    return BA_NONE;
  }

  bool p1_to_act, p2_to_act;
  WhoseAction(&actions, &p1_to_act, &p2_to_act);
  if (! p1_to_act && ! p2_to_act) {
    for (unsigned int p1 = 0; p1 <= 1; ++p1) {
      for (unsigned int st = 0; st <= max_street; ++st) {
	delete [] current_buckets[p1][st];
      }
      delete [] current_buckets[p1];
    }
    delete [] current_buckets;
    return BA_NONE;
  }
  if (p1 != p1_to_act) {
    for (unsigned int p1 = 0; p1 <= 1; ++p1) {
      for (unsigned int st = 0; st <= max_street; ++st) {
	delete [] current_buckets[p1][st];
      }
      delete [] current_buckets[p1];
    }
    delete [] current_buckets;
    return BA_NONE;
  }

  bool terminate;
  unsigned int last_bet_to;
  vector<Node *> path;
  Node *sob_node;
  ProcessActions(&actions, p1, hand_index, &path, &sob_node, &terminate,
		 &last_bet_to);
  if (debug_ && sob_node) {
    fprintf(stderr, "ProcessActions returned with sob node\n");
  }
  if (terminate) {
    for (unsigned int p1 = 0; p1 <= 1; ++p1) {
      for (unsigned int st = 0; st <= max_street; ++st) {
	delete [] current_buckets[p1][st];
      }
      delete [] current_buckets[p1];
    }
    delete [] current_buckets;
    return BA_NONE;
  }

  // Default
  BotAction bot_action = BA_NONE;

  bool force_all_in;
  unsigned int opp_bet_amount;
  double *probs = NULL;
  bool probs_set = false;

  if (hard_coded_root_strategy_ && path.size() == 1 && p1) {
    // A hack we may want to employ to enforce a certain strategy at the root.
    probs = new double[2];
    unsigned int b = current_buckets[1][0][0];
    opp_bet_amount = 0;
    force_all_in = false;
    probs_set = true;

    // Folds below were those hands that lose -50 or more when Slumbot 2015
    //   plays itself:
    //     32o (2), 42o (5), 62o (17), 82o (37), 83o (39), 72o (26).
    // Old:
    //   if (b == 2 || b == 5 || b == 17 || b == 26 || b == 37 || b == 39) {

    // New
    // Try also open folding 52o (10), 73o (28), 74o (30), 92o (50), 93o (52).
    if (b == 2 || b == 5 || b == 17 || b == 26 || b == 37 || b == 39 ||
	b == 10 || b == 28 || b == 30 || b == 50 || b == 52) {
      probs[0] = 1.0;
      probs[1] = 0;
    } else {
      probs[0] = 0;
      probs[1] = 1.0;
    }
  }

  bool force_call = false;
  if (! probs_set) {
    probs = GetActionProbs(actions, &path, sob_node, hand_index,
			   current_buckets, p1, &opp_bet_amount,
			   &force_all_in, &force_call);
  }

  Node *current_node = path[path.size() - 1];
  unsigned int num_succs = current_node->NumSuccs();
  unsigned int csi = current_node->CallSuccIndex();
  unsigned int fsi = current_node->FoldSuccIndex();

  if (hard_coded_r200_strategy_ && path.size() == 2 && ! p1 &&
      current_node == p0_tree_->Root()->IthSucc(2)) {

    unsigned int b = current_buckets[0][0][0];

    // Try r200 folding only 62o (17), 72o (26), 73o (28), 82o (37), 83o (39),
    //   92o (50).
    if (b == 17 || b == 26 || b == 28 || b == 37 || b == 39 || b == 50) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (s == fsi) probs[s] = 1.0;
	else          probs[s] = 0;
      }
    } else {
      // Force no-fold
      double fold_prob = probs[fsi];
      if (fold_prob == 1.0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == csi) probs[s] = 1.0;
	  else          probs[s] = 0;
	}
      } else if (fold_prob == 0) {
	// Nothing to do
      } else {
	// Set fold prob to zero; scale up all other probs
	double scale_up = 1.0 / (1.0 - fold_prob);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == fsi) probs[s] = 0;
	  else          probs[s] *= scale_up;
	}
      }
    }
  }

  if (hard_coded_r250_strategy_ && path.size() == 2 && ! p1 &&
      current_node == p0_tree_->Root()->IthSucc(3)) {

    unsigned int b = current_buckets[0][0][0];

    // Try r250 folding only 32o (2), 42o (5), 52o (10), 62o (17), 63o (19),
    // 72o (26), 73o (28), 74o (30), 82o (37), 83o (39), 84o (41), 92o (50),
    // 93o (52), T2o (65), T3o (67), J2o (82)
    if (b == 2 || b == 5 || b == 10 || b == 17 || b == 19 || b == 26 ||
	b == 28 || b == 30 || b == 37 || b == 39 || b == 41 || b == 50 ||
	b == 52 || b == 65 || b == 67 || b == 82) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (s == fsi) probs[s] = 1.0;
	else          probs[s] = 0;
      }
    } else {
      // Force no-fold
      double fold_prob = probs[fsi];
      if (fold_prob == 1.0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == csi) probs[s] = 1.0;
	  else          probs[s] = 0;
	}
      } else if (fold_prob == 0) {
	// Nothing to do
      } else {
	// Set fold prob to zero; scale up all other probs
	double scale_up = 1.0 / (1.0 - fold_prob);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == fsi) probs[s] = 0;
	  else          probs[s] *= scale_up;
	}
      }
    }
  }

  if (hard_coded_r200r600_strategy_ && path.size() == 3 && p1 &&
      current_node == p1_tree_->Root()->IthSucc(1)->IthSucc(2)) {

    unsigned int b = current_buckets[0][0][0];
    // Try r200r600 folding only 42 hands:
    // 32o (2), 42o (5), 43o (7), 52o (10), 53o (12), 62o (17), 63o (19),
    // 64o (21), 72o (26), 73o (28), 74o (30), 75o (32), 82o (37), 83o (39),
    // 84o (41), 85o (43), 86o (45), 92o (50), 93o (52), 94o (54), 95o (56),
    // 96o (58), T2o (65), T3o (67), T4o (69), T5o (71), T6o (73), J2o (82),
    // J3o (84), J4o (86), J5o (88), Q2o (101), Q3o (103), Q4o (105),
    // Q5o (107), K2o (122); 32s (1), 62s (16), 72s (25), 73s (27), 82s (36),
    // 83s (38).
    // After final testing, adding 15 hands to make 57:
    //   84s (40), 92s-95s (49, 51, 53, 55), 97o (60), T2s-T3s (64, 66),
    //   T7o (75), J6o-J8o (90, 92, 94), Q6o-Q8o (109, 111, 113)
    if (b == 2 || b == 5 || b == 7 || b == 10 || b == 12 || b == 17 ||
	b == 19 || b == 21 || b == 26 || b == 28 || b == 30 || b == 32 ||
	b == 37 || b == 39 || b == 41 || b == 43 || b == 45 || b == 50 ||
	b == 52 || b == 54 || b == 56 || b == 58 || b == 65 || b == 67 ||
	b == 69 || b == 71 || b == 73 || b == 82 || b == 84 || b == 86 ||
	b == 88 || b == 101 || b == 103 || b == 105 || b == 107 || b == 122 ||
	b == 1 || b == 16 || b == 25 || b == 27 || b == 36 || b == 38 ||
	// New additions
	b == 40 || b == 49 || b == 51 || b == 53 || b == 55 || b == 60 ||
	b == 64 || b == 66 || b == 75 || b == 90 || b == 92 || b == 94 ||
	b == 109 || b == 111 || b == 113) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (s == fsi) probs[s] = 1.0;
	else          probs[s] = 0;
      }
    } else {
      // Force no-fold
      double fold_prob = probs[fsi];
      if (fold_prob == 1.0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == csi) probs[s] = 1.0;
	  else          probs[s] = 0;
	}
      } else if (fold_prob == 0) {
	// Nothing to do
      } else {
	// Set fold prob to zero; scale up all other probs
	double scale_up = 1.0 / (1.0 - fold_prob);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == fsi) probs[s] = 0;
	  else          probs[s] *= scale_up;
	}
      }
    }
  }

  if (hard_coded_r200r800_strategy_ && path.size() == 3 && p1 &&
      current_node == p1_tree_->Root()->IthSucc(1)->IthSucc(3)) {

    unsigned int b = current_buckets[0][0][0];
    // Try r200r800 folding only 62 hands:
    // The 42 hands we fold at r200r600:
    // 32o (2), 42o (5), 43o (7), 52o (10), 53o (12), 62o (17), 63o (19),
    // 64o (21), 72o (26), 73o (28), 74o (30), 75o (32), 82o (37), 83o (39),
    // 84o (41), 85o (43), 86o (45), 92o (50), 93o (52), 94o (54), 95o (56),
    // 96o (58), T2o (65), T3o (67), T4o (69), T5o (71), T6o (73), J2o (82),
    // J3o (84), J4o (86), J5o (88), Q2o (101), Q3o (103), Q4o (105),
    // Q5o (107), K2o (122); 32s (1), 62s (16), 72s (25), 73s (27), 82s (36),
    // 83s (38).
    // Plus eleven more suited hands:
    // 74s (29), 84s (40), 92s (49), 93s (51), 94s (53), T2s (64), T3s (66),
    // T4s (68), T5s (70), T6s (72), J2s (81)
    // Plus nine more unsuited hands:
    // J6o (90), J7o (92), Q6o (109), Q7o (111), K3o (124), K4o (126),
    // K5o (128), K6o (130), K7o (132)
    // The five new hands we added to r200r600 that weren't already here:
    // 95s (55), 97o (60), T7o (75), J8o (94), Q8o (113)
    // The 6 additional hands that were losing to Tartanian8, and which we
    // fold in the base:
    // 52s (9), T8o (77), J3s (83), Q2s-Q4s (100, 102, 104)
    // Note: base does not fold 63s-64s, 75s, 85s-86s, T7s
    if (b == 2 || b == 5 || b == 7 || b == 10 || b == 12 || b == 17 ||
	b == 19 || b == 21 || b == 26 || b == 28 || b == 30 || b == 32 ||
	b == 37 || b == 39 || b == 41 || b == 43 || b == 45 || b == 50 ||
	b == 52 || b == 54 || b == 56 || b == 58 || b == 65 || b == 67 ||
	b == 69 || b == 71 || b == 73 || b == 82 || b == 84 || b == 86 ||
	b == 88 || b == 101 || b == 103 || b == 105 || b == 107 || b == 122 ||
	b == 1 || b == 16 || b == 25 || b == 27 || b == 36 || b == 38 ||
	b == 29 || b == 40 || b == 49 || b == 51 || b == 53 || b == 64 ||
	b == 66 || b == 68 || b == 70 || b == 72 || b == 81 ||
	b == 90 || b == 92 || b == 109 || b == 111 || b == 124 || b == 126 ||
	b == 128 || b == 130 || b == 132 ||
	b == 55 || b == 60 || b == 75 || b == 94 || b == 113 ||
	b == 9 || b == 77 || b == 83 || b == 100 || b == 102 || b == 104) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (s == fsi) probs[s] = 1.0;
	else          probs[s] = 0;
      }
    } else {
      // Force no-fold
      double fold_prob = probs[fsi];
      if (fold_prob == 1.0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == csi) probs[s] = 1.0;
	  else          probs[s] = 0;
	}
      } else if (fold_prob == 0) {
	// Nothing to do
      } else {
	// Set fold prob to zero; scale up all other probs
	double scale_up = 1.0 / (1.0 - fold_prob);
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == fsi) probs[s] = 0;
	  else          probs[s] *= scale_up;
	}
      }
    }
  }

  if (force_call) {
    bot_action = BA_CALL;
  } else {
    // I better reseed to make sure I don't get the seed used in translation.
    // I seed the RNG to something based on the hand index and the action index.
    // So behaviour should be replicable, but only for the same hand and same
    // action.
    unsigned int seed = hand_index * 971 + (p1 ? 23 : 0) + path.size() * 7;
    SeedRand(seed);
    double r = RandZeroToOne();
    if (debug_) {
      fprintf(stderr, "r %f\n", r);
    }
    double cum = 0;
    if (num_succs == 0) {
      fprintf(stderr, "Can't handle zero succs\n");
      if (exit_on_error_) exit(-1);
    }
    unsigned int s;
    for (s = 0; s < num_succs - 1; ++s) {
      cum += probs[s];
      if (debug_) {
	fprintf(stderr, "s %u prob %f cum %f\n", s, probs[s], cum);
      }
      if (r < cum) break;
    }
    delete [] probs;

    if (s == csi) {
      if (force_all_in) {
	bot_action = BA_BET;
	*we_bet_to = stack_size_;
      } else {
	bot_action = BA_CALL;
      }
    } else if (s == fsi) {
      bot_action = BA_FOLD;
    } else {
      bot_action = BA_BET;
      
      Node *b = current_node->IthSucc(s);
      *we_bet_to = b->LastBetTo() * small_blind_;
      // Note that our_bet_size can be negative
      int our_bet_size = (int)*we_bet_to - (int)last_bet_to;
      if (debug_) {
	fprintf(stderr, "s %i lbt %i wbt %u our_bet_size %i\n", s, last_bet_to,
		*we_bet_to, our_bet_size);
      }
      
      // Make sure we bet at least one big blind.  (Note: in case that's more
      // than all-in, bet will be lowered below.)
      if (our_bet_size < (int)(2 * small_blind_)) {
	our_bet_size = (int)(2 * small_blind_);
	*we_bet_to = last_bet_to + our_bet_size;
      }
      
      // Make sure our raise is at least size of opponent's bet.  (Note: in case
      // that's more than all-in, bet will be lowered below.)
      if (our_bet_size < (int)opp_bet_amount) {
	our_bet_size = opp_bet_amount;
	*we_bet_to = last_bet_to + our_bet_size;
      }

      // Make sure we are not betting more than all-in.
      if (*we_bet_to > stack_size_) {
	*we_bet_to = stack_size_;
	// Could be zero, in which case we should turn this bet into a call
	our_bet_size = (int)*we_bet_to - (int)last_bet_to;
      }

      // If bet size is zero, change action to a call.
      if (our_bet_size == 0) {
	bot_action = BA_CALL;
	*we_bet_to = 0;
      }

      if (debug_) {
	fprintf(stderr, "selected succ %u bet_to %u bet amount %u\n", s,
		*we_bet_to, our_bet_size);
      }
    }
  }

  for (unsigned int p1 = 0; p1 <= 1; ++p1) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      delete [] current_buckets[p1][st];
    }
    delete [] current_buckets[p1];
  }
  delete [] current_buckets;

  return bot_action;
}
