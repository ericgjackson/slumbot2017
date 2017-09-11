// Intended to be used for final system.  Goals:
// 1) Hard-codes a lot of decisions (i.e., what bet sizes are allowed).
// These are not controlled by parameters.
// 2) Asymmetric systems.  Lots of bet sizes for opponent, few for us.
// 3) Assume rivers will always be resolved.  Therefore very few betting
// options on river.
// 4) Assume turns with large pots will always be resolved, but not those
// with small pots.  Therefore very few betting options when pot is large,
// but lots when pot is small.
// 5) Geometric bet sizes?  If two bets are required to get all-in, allow
// bet size such that two identical bet sizes (in terms of pot frac) gets
// us all-in.
//
// What do I want to do with illegally small bets, especially raises?  Should
// I round them up to a min-raise?  What if a min-raise is very close to the
// next largest raise size?  Need more pruning perhaps.
// I use ints in some places.  Is that necessary?  Are negative values
// possible?

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "betting_tree_builder.h"
#include "constants.h"
#include "game.h"
// #include "pool.h"
#include "split.h"

// Return if we are close to all-in.  Typical use of this is that we will
// only allow an all-in bet for ourselves if we are close to all-in.
// Our current definition of "close" is that a pot size bet would get us
// all in.
bool BettingTreeBuilder::CloseToAllIn(int old_pot_size) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  return (old_pot_size > 0.33333 * all_in_pot_size);
}

bool BettingTreeBuilder::GoGeometric(int old_pot_size) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  return (old_pot_size > 0.2 * all_in_pot_size);
}

void BettingTreeBuilder::AddFracBet(int old_pot_size, double frac,
				    bool *pot_size_seen) {
  double double_bet_size = old_pot_size * frac;
  int bet_size = (int)(double_bet_size + 0.5);
  // Can this happen?
  if (bet_size == 0) return;
  int new_pot_size = old_pot_size + 2 * bet_size;
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  if (new_pot_size > all_in_pot_size) {
    pot_size_seen[all_in_pot_size] = true;
  } else {
    pot_size_seen[new_pot_size] = true;
  }
}

void BettingTreeBuilder::AddOurPreflopBet(int old_pot_size,
					  unsigned int player_acting,
					  bool *pot_size_seen,
					  unsigned int num_street_bets) {
  if (CloseToAllIn(old_pot_size)) {
    unsigned int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    pot_size_seen[all_in_pot_size] = true;
    return;
  }
  if (GoGeometric(old_pot_size)) {
    // The pot is too small to simply only allow all-ins.  So instead
    // pick a pot frac such that two bets of that size will get us all-in.
    int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    double ratio = all_in_pot_size / old_pot_size;
    double pot_frac = sqrt(ratio);
    double bet_frac = (pot_frac - 1.0) / 2.0;
    AddFracBet(old_pot_size, bet_frac, pot_size_seen);
    return;
  }
  if (num_street_bets == 0) {
    // if (player_acting == Game::FirstToAct(0)) {
    // Research says this is all we need.  But I did this research on
    // a 20-card deck.  And I didn't distinguish P1's opening bet from
    // P0's opening bet after a limp.
    AddFracBet(old_pot_size, 0.5, pot_size_seen);
    AddFracBet(old_pot_size, 0.75, pot_size_seen);
  } else if (num_street_bets == 1) {
    // Experience from prior years suggest that large raises preflop are
    // often advantageous.
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
    AddFracBet(old_pot_size, 2.0, pot_size_seen);
    AddFracBet(old_pot_size, 3.0, pot_size_seen);
  } else {
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
  }
}

void BettingTreeBuilder::AddOurFlopBet(int old_pot_size, bool *pot_size_seen,
				       unsigned int num_street_bets) {
  if (CloseToAllIn(old_pot_size)) {
    unsigned int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    pot_size_seen[all_in_pot_size] = true;
    return;
  }
  if (GoGeometric(old_pot_size)) {
    // The pot is too small to simply only allow all-ins.  So instead
    // pick a pot frac such that two bets of that size will get us all-in.
    int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    double ratio = all_in_pot_size / old_pot_size;
    double pot_frac = sqrt(ratio);
    double bet_frac = (pot_frac - 1.0) / 2.0;
    AddFracBet(old_pot_size, bet_frac, pot_size_seen);
    return;
  }
  if (num_street_bets == 0) {
    // 1/4, 1/2, 1.5x
    AddFracBet(old_pot_size, 0.25, pot_size_seen);
    AddFracBet(old_pot_size, 0.5, pot_size_seen);
    AddFracBet(old_pot_size, 1.5, pot_size_seen);
  } else if (num_street_bets == 1) {
    // 1/2, 1x, 1.5x
    // Not a lot of research on what to do here
    AddFracBet(old_pot_size, 0.5, pot_size_seen);
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
    AddFracBet(old_pot_size, 1.5, pot_size_seen);
  } else {
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
  }
}

void BettingTreeBuilder::AddOurTurnBet(int old_pot_size, bool *pot_size_seen,
				       unsigned int num_street_bets) {
  if (CloseToAllIn(old_pot_size)) {
    unsigned int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    pot_size_seen[all_in_pot_size] = true;
    return;
  }
  if (GoGeometric(old_pot_size)) {
    // The pot is too small to simply only allow all-ins.  So instead
    // pick a pot frac such that two bets of that size will get us all-in.
    int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    double ratio = all_in_pot_size / old_pot_size;
    double pot_frac = sqrt(ratio);
    double bet_frac = (pot_frac - 1.0) / 2.0;
    AddFracBet(old_pot_size, bet_frac, pot_size_seen);
    return;
  }
  // Note that if we get here the pot is relatively modest sized
  if (num_street_bets == 0) {
    // 1/4, 1/2, 1.5x
    AddFracBet(old_pot_size, 0.25, pot_size_seen);
    AddFracBet(old_pot_size, 0.5, pot_size_seen);
    AddFracBet(old_pot_size, 1.5, pot_size_seen);
  } else if (num_street_bets == 1) {
    // 1/2, 1x, 1.5x
    // Not a lot of research on what to do here
    AddFracBet(old_pot_size, 0.5, pot_size_seen);
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
    AddFracBet(old_pot_size, 1.5, pot_size_seen);
  } else {
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
  }
}

void BettingTreeBuilder::AddOurRiverBet(int old_pot_size,
					bool *pot_size_seen) {
  if (CloseToAllIn(old_pot_size)) {
    unsigned int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    pot_size_seen[all_in_pot_size] = true;
    return;
  }
  if (GoGeometric(old_pot_size)) {
    // The pot is too small to simply only allow all-ins.  So instead
    // pick a pot frac such that two bets of that size will get us all-in.
    int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    double ratio = all_in_pot_size / old_pot_size;
    double pot_frac = sqrt(ratio);
    double bet_frac = (pot_frac - 1.0) / 2.0;
    AddFracBet(old_pot_size, bet_frac, pot_size_seen);
    return;
  }
  // Only allow pot-size bets.  Remember that we will be resolving all rivers.
  AddFracBet(old_pot_size, 1.0, pot_size_seen);
}

// Seven opponent preflop bet sizes
void BettingTreeBuilder::AddOppPreflopBet(int old_pot_size,
					  bool *pot_size_seen) {
  AddFracBet(old_pot_size, 0.5, pot_size_seen);
  AddFracBet(old_pot_size, 0.75, pot_size_seen);
  AddFracBet(old_pot_size, 1.0, pot_size_seen);
  AddFracBet(old_pot_size, 1.5, pot_size_seen);
  AddFracBet(old_pot_size, 2.0, pot_size_seen);
  AddFracBet(old_pot_size, 3.0, pot_size_seen);
  AddFracBet(old_pot_size, 5.0, pot_size_seen);
}

// Eight opponent flop bet sizes
void BettingTreeBuilder::AddOppFlopBet(int old_pot_size, bool *pot_size_seen,
				       unsigned int num_street_bets) {
  if (num_street_bets == 0) {
    AddFracBet(old_pot_size, 0.25, pot_size_seen);
  }
  AddFracBet(old_pot_size, 0.5, pot_size_seen);
  AddFracBet(old_pot_size, 0.75, pot_size_seen);
  AddFracBet(old_pot_size, 1.0, pot_size_seen);
  AddFracBet(old_pot_size, 1.5, pot_size_seen);
  AddFracBet(old_pot_size, 2.0, pot_size_seen);
  AddFracBet(old_pot_size, 3.0, pot_size_seen);
  AddFracBet(old_pot_size, 5.0, pot_size_seen);
}

void BettingTreeBuilder::AddOppTurnBet(int old_pot_size, bool *pot_size_seen,
				       unsigned int num_street_bets) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  if (old_pot_size > 0.2 * all_in_pot_size) {
    // These turn situations will be resolved.  So we don't need to offer
    // a lot of bet sizes.
    if (CloseToAllIn(old_pot_size)) {
      pot_size_seen[all_in_pot_size] = true;
      return;
    }
    if (GoGeometric(old_pot_size)) {
      // The pot is too small to simply only allow all-ins.  So instead
      // pick a pot frac such that two bets of that size will get us all-in.
      double ratio = all_in_pot_size / old_pot_size;
      double pot_frac = sqrt(ratio);
      double bet_frac = (pot_frac - 1.0) / 2.0;
      AddFracBet(old_pot_size, bet_frac, pot_size_seen);
      return;
    }
    // Currently don't get here, but if we tweak the thresholds for large turn
    // pots and/or GoGeometric(), we could in the future.
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
  } else {
    // Small pot so allow full range of opponent bets
    if (num_street_bets == 0) {
      AddFracBet(old_pot_size, 0.25, pot_size_seen);
    }
    AddFracBet(old_pot_size, 0.5, pot_size_seen);
    AddFracBet(old_pot_size, 0.75, pot_size_seen);
    AddFracBet(old_pot_size, 1.0, pot_size_seen);
    AddFracBet(old_pot_size, 1.5, pot_size_seen);
    AddFracBet(old_pot_size, 2.0, pot_size_seen);
    AddFracBet(old_pot_size, 3.0, pot_size_seen);
    AddFracBet(old_pot_size, 5.0, pot_size_seen);
  }
}

// River subgames are always resolved.  So we don't need to offer
// a lot of bet sizes.
void BettingTreeBuilder::AddOppRiverBet(int old_pot_size, bool *pot_size_seen,
					unsigned int num_street_bets) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  if (CloseToAllIn(old_pot_size)) {
    pot_size_seen[all_in_pot_size] = true;
    return;
  }
  if (GoGeometric(old_pot_size)) {
    // The pot is too small to simply only allow all-ins.  So instead
    // pick a pot frac such that two bets of that size will get us all-in.
    int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    double ratio = all_in_pot_size / old_pot_size;
    double pot_frac = sqrt(ratio);
    double bet_frac = (pot_frac - 1.0) / 2.0;
    AddFracBet(old_pot_size, bet_frac, pot_size_seen);
    return;
  }
  // Currently don't get here, but if we tweak the thresholds for large turn
  // pots and/or GoGeometric(), we could in the future.
  AddFracBet(old_pot_size, 1.0, pot_size_seen);
}

// old_pot_size includes the previous bet (whose size is last_bet_size)
void BettingTreeBuilder::AddBets(unsigned int st, unsigned int player_acting,
				 unsigned int target_player, int old_pot_size,
				 unsigned int num_street_bets,
				 unsigned int last_bet_size,
				 vector<Node *> *bet_succs,
				 unsigned int *terminal_id) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  // Already all-in; no bets possible
  if (old_pot_size == all_in_pot_size) return;
  unique_ptr<bool []> pot_size_seen(new bool[all_in_pot_size + 1]);
  for (int ps = 0; ps <= all_in_pot_size; ++ps) {
    pot_size_seen[ps] = false;
  }
  bool opp = player_acting != target_player;
  if (st == 0) {
    if (opp) {
      AddOppPreflopBet(old_pot_size, pot_size_seen.get());
    } else {
      AddOurPreflopBet(old_pot_size, player_acting, pot_size_seen.get(),
		       num_street_bets);
    }
  } else if (st == 1) {
    if (opp) {
      AddOppFlopBet(old_pot_size, pot_size_seen.get(), num_street_bets);
    } else {
      AddOurFlopBet(old_pot_size, pot_size_seen.get(), num_street_bets);
    }
  } else if (st == 2) {
    if (opp) {
      AddOppTurnBet(old_pot_size, pot_size_seen.get(), num_street_bets);
    } else {
      AddOurTurnBet(old_pot_size, pot_size_seen.get(), num_street_bets);
    }
  } else if (st == 3) {
    if (opp) {
      AddOppRiverBet(old_pot_size, pot_size_seen.get(), num_street_bets);
    } else {
      AddOurRiverBet(old_pot_size, pot_size_seen.get());
    }
  } else {
    fprintf(stderr, "Unexpected street %u\n", st);
    exit(-1);
  }

  // Always allow an all-in bet
  pot_size_seen[all_in_pot_size] = true;

  vector<unsigned int> pot_sizes;
  for (int p = 0; p <= all_in_pot_size; ++p) {
    if (pot_size_seen[p]) {
      pot_sizes.push_back(p);
    }
  }

  // Possibly prune last non-all-in bet.  We could in some cases end up with
  // a non-all-in bet that is very close to an all-in bet.  Wasteful to keep
  // it.
  if (pot_sizes.size() >= 2) {
    int ntl_pot = pot_sizes[pot_sizes.size() - 2];
    int ntl_bet = (ntl_pot - old_pot_size) / 2;
    int ai_bet = (all_in_pot_size - old_pot_size) / 2;
    double ntl_frac = ((double)ntl_bet) / (double)old_pot_size;
    double ai_frac = ((double)ai_bet) / (double)old_pot_size;
    if (ntl_frac > 0.75 * ai_frac) {
      pot_sizes[pot_sizes.size() - 2] = all_in_pot_size;
      pot_sizes.resize(pot_sizes.size() - 1);
    }
  }

  bet_succs->clear();
  unsigned int num_pot_sizes = pot_sizes.size();
  for (unsigned int i = 0; i < num_pot_sizes; ++i) {
    int pot_size = pot_sizes[i];
    unsigned int bet_size = (pot_size - old_pot_size) / 2;
    bool all_in_bet = (pot_size == all_in_pot_size);

    // Cannot make a bet that is smaller than the min bet (usually the big
    // blind).  Exception is that you can always bet all-in
    if (bet_size < betting_abstraction_.MinBet() && ! all_in_bet) {
      continue;
    }

    // In general, cannot make a bet that is smaller than the previous
    // bet size.  Exception is that you can always raise all-in
    if (bet_size < last_bet_size && ! all_in_bet) {
      continue;
    }

    // For bets we pass in the pot size without the pending bet included
    Node *bet = CreateNoLimitSubtree3(st, old_pot_size, bet_size,
				      num_street_bets + 1, player_acting,
				      player_acting^1, target_player,
				      terminal_id);
    bet_succs->push_back(bet);
  }
}

// May return NULL
Node *BettingTreeBuilder::CreateCallSucc3(unsigned int street,
					  unsigned int last_pot_size,
					  unsigned int last_bet_size,
					  unsigned int num_street_bets,
					  bool p1_last_bettor,
					  unsigned int player_acting,
					  unsigned int target_player,
					  unsigned int *terminal_id) {
  unsigned int after_call_pot_size = last_pot_size + 2 * last_bet_size;
  // We advance the street if we are calling a bet
  // Note that a call on the final street is considered to advance the street
  bool advance_street = num_street_bets > 0;
  // This assumes heads-up
  advance_street |= (Game::FirstToAct(street) != player_acting);
  Node *call_succ;
  unsigned int max_street = Game::MaxStreet();
  if (street < max_street && advance_street) {
    // Assumes player 0 first to act postflop
    call_succ = CreateNoLimitSubtree3(street + 1, after_call_pot_size, 0, 0,
				      p1_last_bettor, 0, target_player,
				      terminal_id);
  } else if (! advance_street) {
    // This is a check that does not advance the street
    call_succ = CreateNoLimitSubtree3(street, after_call_pot_size, 0, 0,
				      p1_last_bettor, player_acting^1,
				      target_player, terminal_id);
  } else {
    // This is a call on the final street
    call_succ = new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			 nullptr, 255, after_call_pot_size);

  }
  return call_succ;
}

// There are several pot sizes here which is confusing.  When we call
// CreateNoLimitSuccs() it may be as the result of adding a bet action.
// So we have the pot size before that bet, the pot size after that bet,
// and potentially the pot size after a raise of the bet.  last_pot_size is
// the first pot size - i.e., the pot size before the pending bet.
// current_pot_size is the pot size after the pending bet.
void BettingTreeBuilder::CreateNoLimitSuccs3(unsigned int st,
					     unsigned int last_pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_street_bets,
					     bool p1_last_bettor,
					     unsigned int player_acting,
					     unsigned int target_player,
					     unsigned int *terminal_id,
					     Node **call_succ,
					     Node **fold_succ,
					     vector<Node *> *bet_succs) {
  *fold_succ = NULL;
  *call_succ = NULL;
  bet_succs->clear();
  bool no_open_limp = 
    ((! betting_abstraction_.Asymmetric() &&
      betting_abstraction_.NoOpenLimp()) ||
     (betting_abstraction_.Asymmetric() && player_acting == target_player &&
      betting_abstraction_.OurNoOpenLimp()) ||
     (betting_abstraction_.Asymmetric() && player_acting != target_player &&
      betting_abstraction_.OppNoOpenLimp()));
  if (! (st == 0 && num_street_bets == 0 &&
	 player_acting == Game::FirstToAct(0) && no_open_limp)) {
    *call_succ = CreateCallSucc3(st, last_pot_size, last_bet_size,
				 num_street_bets, p1_last_bettor,
				 player_acting, target_player, terminal_id);
  }
  if (num_street_bets > 0 || (st == 0 && num_street_bets == 0 &&
			      Game::BigBlind() > Game::SmallBlind() &&
			      last_pot_size < 2 * Game::BigBlind())) {
    *fold_succ = CreateFoldSucc(st, last_pot_size, player_acting,
				terminal_id);
  }

  int after_bet_size = last_pot_size + 2 * last_bet_size;
  AddBets(st, player_acting, target_player, after_bet_size, num_street_bets,
	  last_bet_size, bet_succs, terminal_id);
}

Node *BettingTreeBuilder::CreateNoLimitSubtree3(unsigned int street,
						unsigned int pot_size,
						unsigned int last_bet_size,
						unsigned int num_street_bets,
						bool p1_last_bettor,
						unsigned int player_acting,
						unsigned int target_player,
						unsigned int *terminal_id) {
  Node *call_succ = NULL, *fold_succ = NULL;
  vector<Node *> bet_succs;
  CreateNoLimitSuccs3(street, pot_size, last_bet_size, num_street_bets,
		      p1_last_bettor, player_acting, target_player,
		      terminal_id, &call_succ, &fold_succ, &bet_succs);
  if (call_succ == NULL && fold_succ == NULL && bet_succs.size() == 0) {
    fprintf(stderr, "Creating nonterminal with zero succs\n");
    fprintf(stderr, "This will cause problems\n");
    exit(-1);
  }
  // Assign nonterminal ID of zero for now.  Will get updated at runtime
  // after tree is read from disk.
  Node *node = new Node(0, street, player_acting, call_succ, fold_succ,
			&bet_succs, 255, pot_size);
  return node;
}

Node *BettingTreeBuilder::CreateNoLimitTree3(unsigned int street,
					     unsigned int pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_street_bets,
					     bool p1_last_bettor,
					     unsigned int player_acting,
					     unsigned int target_player,
					     unsigned int *terminal_id) {
  return CreateNoLimitSubtree3(street, pot_size, last_bet_size, num_street_bets,
			       p1_last_bettor, player_acting, target_player,
			       terminal_id);
}

