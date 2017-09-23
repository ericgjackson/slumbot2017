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

// Produces the new possible pot sizes.  Assumes any size bet is allowed by
// the abstraction (well, only multiples of the small blind).
// This function does not rule out certain illegal bet sizes; we do not
// check that the bet size is at least one big blind and we do not check
// that a raise is at least as big as the previous bet.  These restrictions
// are enforced in HandleBet().
// If there is a pending bet, old_pot_size reflects the size of the pot
// including that bet *and* a call of that bet.
void BettingTreeBuilder::GetAllNewPotSizes(int old_pot_size,
					   vector<int> *new_pot_sizes) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  // Already all-in; no bets possible
  if (old_pot_size == all_in_pot_size) return;
  int one_player_committed = old_pot_size / 2;
  int remaining = betting_abstraction_.StackSize() - one_player_committed;
  for (int bet_size = 1; bet_size <= remaining; ++bet_size) {
    new_pot_sizes->push_back(old_pot_size + 2 * bet_size);
  }
}

// Produces the new possible pot sizes.  Assumes any even size bet is allowed
// by the abstraction (i.e., even multiples of the small blind).
// This function does not rule out certain illegal bet sizes; we do not check
// that a raise is at least as big as the previous bet.  These restrictions
// are enforced in HandleBet().
// If there is a pending bet, old_pot_size reflects the size of the pot
// including that bet *and* a call of that bet.
void BettingTreeBuilder::GetAllNewEvenPotSizes(int old_pot_size,
					       vector<int> *new_pot_sizes) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  // Already all-in; no bets possible
  if (old_pot_size == all_in_pot_size) return;
  int one_player_committed = old_pot_size / 2;
  int remaining = betting_abstraction_.StackSize() - one_player_committed;
  for (int bet_size = 2; bet_size <= remaining; bet_size += 2) {
    new_pot_sizes->push_back(old_pot_size + 2 * bet_size);
  }
}

// Add one bet size if pot size is >= 1/9 of all-in
bool BettingTreeBuilder::AddGeometric1Bet(int old_pot_size,
					  bool *pot_size_seen) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  if (old_pot_size > 0.33333 * all_in_pot_size) {
    // One bet left - all in
    pot_size_seen[all_in_pot_size] = true;
    return true;
    // } else if (old_pot_size >= 0.11112 * all_in_pot_size) {
  } else if (old_pot_size >= 0.2 * all_in_pot_size) {
    double ratio = all_in_pot_size / old_pot_size;
    double pot_frac = sqrt(ratio);
    double bet_frac = (pot_frac - 1.0) / 2.0;
    double double_bet_size = old_pot_size * bet_frac;
    int bet_size = (int)(double_bet_size + 0.5);
    if (bet_size == 0) return false;
    int new_pot_size = old_pot_size + 2 * bet_size;
    if (new_pot_size > all_in_pot_size) {
      pot_size_seen[all_in_pot_size] = true;
    } else {
      pot_size_seen[new_pot_size] = true;
    }
    return true;
  } else {
    return false;
  }
}

// Should be a superset of geometric-1 bets so that we can do
// asymmetric evaluations.
bool BettingTreeBuilder::AddGeometric2Bet(int old_pot_size,
					  bool *pot_size_seen) {
  
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  if (old_pot_size >= 0.33333 * all_in_pot_size) {
    // One bet left - all in
    pot_size_seen[all_in_pot_size] = true;
    return true;
    // } else if (old_pot_size >= 0.11112 * all_in_pot_size) {
  } else if (old_pot_size >= 0.2 * all_in_pot_size) {
    // Allow 1/2 pot, full pot and geometric bet
    for (unsigned int num = 1; num <= 2; ++num) {
      double frac = ((double)num) / 2.0;
      double double_bet_size = old_pot_size * frac;
      int bet_size = (int)(double_bet_size + 0.5);
      if (bet_size > 0) {
	int new_pot_size = old_pot_size + 2 * bet_size;
	if (new_pot_size > all_in_pot_size) {
	  pot_size_seen[all_in_pot_size] = true;
	} else {
	  pot_size_seen[new_pot_size] = true;
	}
      }
    }
    double ratio = all_in_pot_size / old_pot_size;
    double pot_frac = sqrt(ratio);
    double bet_frac = (pot_frac - 1.0) / 2.0;
    double double_bet_size = old_pot_size * bet_frac;
    int bet_size = (int)(double_bet_size + 0.5);
    if (bet_size > 0) {
      int new_pot_size = old_pot_size + 2 * bet_size;
      if (new_pot_size > all_in_pot_size) {
	pot_size_seen[all_in_pot_size] = true;
      } else {
	pot_size_seen[new_pot_size] = true;
      }
    }
    return true;
  } else {
    return false;
  }
}

// Could rule out pot size of 380 when all_in_pot_size is 400.
// There need to be some additional tests performed on the pot sizes.  For
// example, if these new pot sizes correspond to raises we need to check
// that the raise size is valid (at least one big blind, at least as big
// as the previous bet).
void BettingTreeBuilder::GetNewPotSizes(int old_pot_size,
					const vector<double> &pot_fracs,
					unsigned int player_acting,
					unsigned int target_player,
					bool *pot_size_seen) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  // Already all-in; no bets possible
  if (old_pot_size == all_in_pot_size) return;
  if ((! betting_abstraction_.Asymmetric() &&
	      (unsigned int)old_pot_size >=
	      betting_abstraction_.OnlyPotThreshold()) ||
	     (betting_abstraction_.Asymmetric() &&
	      player_acting == target_player &&
	      (unsigned int)old_pot_size >=
	      betting_abstraction_.OurOnlyPotThreshold()) ||
	     (betting_abstraction_.Asymmetric() &&
	      player_acting != target_player &&
	      (unsigned int)old_pot_size >=
	      betting_abstraction_.OppOnlyPotThreshold())) {
    // Only pot-size bets allowed.  (Well, maybe all-ins also.)  So skip code
    // below that looks at pot_fracs.
    if (old_pot_size > 0) {
      int new_pot_size = 3 * old_pot_size;
      if (new_pot_size > all_in_pot_size) {
	pot_size_seen[all_in_pot_size] = true;
      } else {
	pot_size_seen[new_pot_size] = true;
      }
    }
  } else {
    // Geometric bets are used only as an alternative to the normal
    // method (from pot_fracs) when the pot is big enough.
    bool used_geometric = false;
    if ((! betting_abstraction_.Asymmetric() &&
	 betting_abstraction_.GeometricType() == 1) ||
	(betting_abstraction_.Asymmetric() && player_acting == target_player &&
	 betting_abstraction_.OurGeometricType() == 1) ||
	(betting_abstraction_.Asymmetric() && player_acting != target_player &&
	 betting_abstraction_.OppGeometricType() == 1)) {
      used_geometric = AddGeometric1Bet(old_pot_size, pot_size_seen);
    }
    if ((! betting_abstraction_.Asymmetric() &&
	 betting_abstraction_.GeometricType() == 2) ||
	(betting_abstraction_.Asymmetric() && player_acting == target_player &&
	 betting_abstraction_.OurGeometricType() == 2) ||
	(betting_abstraction_.Asymmetric() && player_acting != target_player &&
	 betting_abstraction_.OppGeometricType() == 2)) {
      used_geometric = AddGeometric2Bet(old_pot_size, pot_size_seen);
    }

    if (! used_geometric) {
      unsigned int num_pot_fracs = pot_fracs.size();
      for (unsigned int i = 0; i < num_pot_fracs; ++i) {
	double frac = pot_fracs[i];
	double double_bet_size = old_pot_size * frac;
	int bet_size = (int)(double_bet_size + 0.5);
	if (bet_size == 0) continue;
	int new_pot_size = old_pot_size + 2 * bet_size;
	if (betting_abstraction_.CloseToAllInFrac() > 0 &&
	    new_pot_size >=
	    all_in_pot_size * betting_abstraction_.CloseToAllInFrac()) {
	  // return (old_pot_size > 0.33333 * all_in_pot_size);
	  // Don't add the bet, but add an all-in bet instead
	  pot_size_seen[all_in_pot_size] = true;
	} else if (new_pot_size > all_in_pot_size) {
	  pot_size_seen[all_in_pot_size] = true;
	} else {
	  pot_size_seen[new_pot_size] = true;
	}
      }
    }
  }
}

// We are contemplating adding a bet.  We might or might not be facing a
// previous bet.  old_after_call_pot_size is the size of the pot after any
// pending bet is called.  new_after_call_pot_size is the size of the pot
// after the new bet we are considering is called.
// Note that pot sizes count money contributed by both players.  So if each
// player starts with 200 chips, max pot size is 400.
void BettingTreeBuilder::HandleBet(unsigned int street,
				   unsigned int last_bet_size,
				   unsigned int old_after_call_pot_size,
				   unsigned int new_after_call_pot_size,
				   unsigned int num_street_bets,
				   unsigned int player_acting,
				   unsigned int target_player,
				   unsigned int *terminal_id,
				   vector< shared_ptr<Node> > *bet_succs) {
  // Obviously pot size can't go down after additional bet
  if (new_after_call_pot_size <= old_after_call_pot_size) return;

  // Integer division is appropriate here because these "after call"
  // pot sizes are always even (since each player has put an equal amount
  // of money into the pot).
  unsigned int new_bet_size =
    (new_after_call_pot_size - old_after_call_pot_size) / 2;

  bool all_in_bet =
    new_after_call_pot_size == 2 * betting_abstraction_.StackSize();

  // Cannot make a bet that is smaller than the min bet (usually the big
  // blind)
  if (new_bet_size < betting_abstraction_.MinBet() && ! all_in_bet) {
    return;
  }

  // In general, cannot make a bet that is smaller than the previous
  // bet size.  Exception is that you can always raise all-in
  if (new_bet_size < last_bet_size && ! all_in_bet) {
    return;
  }

  // For bets we pass in the pot size without the pending bet included
  shared_ptr<Node> bet =
    CreateNoLimitSubtree(street, old_after_call_pot_size, new_bet_size,
			 num_street_bets + 1, player_acting^1, target_player,
			 terminal_id);
  bet_succs->push_back(bet);
}

// May return NULL
shared_ptr<Node>
BettingTreeBuilder::CreateCallSucc(unsigned int street,
				   unsigned int last_pot_size,
				   unsigned int last_bet_size,
				   unsigned int num_street_bets,
				   unsigned int player_acting,
				   unsigned int target_player,
				   unsigned int *terminal_id) {
  unsigned int after_call_pot_size = last_pot_size + 2 * last_bet_size;
  // We advance the street if we are calling a bet
  // Note that a call on the final street is considered to advance the street
  bool advance_street = num_street_bets > 0;
  // This assumes heads-up
  advance_street |= (Game::FirstToAct(street) != player_acting);
  shared_ptr<Node> call_succ;
  unsigned int max_street = Game::MaxStreet();
  if (street < max_street && advance_street) {
    // Assumes player 0 first to act postflop
    call_succ = CreateNoLimitSubtree(street + 1, after_call_pot_size, 0, 0,
				     0, target_player, terminal_id);
  } else if (! advance_street) {
    // This is a check that does not advance the street
    call_succ = CreateNoLimitSubtree(street, after_call_pot_size, 0, 0,
				     player_acting^1, target_player,
				     terminal_id);
  } else {
    // This is a call on the final street
    call_succ.reset(new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			     nullptr, 255, after_call_pot_size));

  }
  return call_succ;
}

shared_ptr<Node>
BettingTreeBuilder::CreateFoldSucc(unsigned int street,
				   unsigned int last_pot_size,
				   unsigned int player_acting,
				   unsigned int *terminal_id) {
  shared_ptr<Node> fold_succ;
  fold_succ.reset(new Node((*terminal_id)++, street, 255, nullptr,
			   nullptr, nullptr, player_acting, last_pot_size));
  return fold_succ;
}

// There are several pot sizes here which is confusing.  When we call
// CreateNoLimitSuccs() it may be as the result of adding a bet action.
// So we have the pot size before that bet, the pot size after that bet,
// and potentially the pot size after a raise of the bet.  last_pot_size is
// the first pot size - i.e., the pot size before the pending bet.
// current_pot_size is the pot size after the pending bet.
void BettingTreeBuilder::CreateNoLimitSuccs(unsigned int street,
					    unsigned int last_pot_size,
					    unsigned int last_bet_size,
					    unsigned int num_street_bets,
					    unsigned int player_acting,
					    unsigned int target_player,
					    unsigned int *terminal_id,
					    shared_ptr<Node> *call_succ,
					    shared_ptr<Node> *fold_succ,
					    vector< shared_ptr<Node> > *
					    bet_succs) {
  // *fold_succ = NULL;
  // *call_succ = NULL;
  bet_succs->clear();
  bool no_open_limp = 
    ((! betting_abstraction_.Asymmetric() &&
      betting_abstraction_.NoOpenLimp()) ||
     (betting_abstraction_.Asymmetric() && player_acting == target_player &&
      betting_abstraction_.OurNoOpenLimp()) ||
     (betting_abstraction_.Asymmetric() && player_acting != target_player &&
      betting_abstraction_.OppNoOpenLimp()));
  if (! (street == 0 && num_street_bets == 0 &&
	 player_acting == Game::FirstToAct(0) && no_open_limp)) {
    *call_succ = CreateCallSucc(street, last_pot_size, last_bet_size,
				num_street_bets, player_acting, target_player,
				terminal_id);
  }
  if (num_street_bets > 0 || (street == 0 && num_street_bets == 0 &&
			      Game::BigBlind() > Game::SmallBlind() &&
			      last_pot_size < 2 * Game::BigBlind())) {
    *fold_succ = CreateFoldSucc(street, last_pot_size, player_acting,
				terminal_id);
  }

  bool our_bet = (target_player == player_acting);
  unsigned int current_pot_size = last_pot_size + 2 * last_bet_size;
  vector<int> new_pot_sizes;
  if (betting_abstraction_.AllBetSizeStreet(street)) {
    if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
      GetAllNewPotSizes(current_pot_size, &new_pot_sizes);
    }
  } else if (betting_abstraction_.AllEvenBetSizeStreet(street)) {
    if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
      GetAllNewEvenPotSizes(current_pot_size, &new_pot_sizes);
    }
  } else {
    int all_in_pot_size = 2 * betting_abstraction_.StackSize();
    bool *pot_size_seen = new bool[all_in_pot_size + 1];
    for (int p = 0; p <= all_in_pot_size; ++p) {
      pot_size_seen[p] = false;
    }
    
    if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
      if ((! betting_abstraction_.Asymmetric() &&
	   betting_abstraction_.AlwaysAllIn()) ||
	  (betting_abstraction_.Asymmetric() &&
	   player_acting == target_player &&
	   betting_abstraction_.OurAlwaysAllIn()) ||
	  (betting_abstraction_.Asymmetric() &&
	   player_acting != target_player &&
	   betting_abstraction_.OppAlwaysAllIn())) {
	// Allow an all-in bet
	pot_size_seen[all_in_pot_size] = true;
      }
    }

    if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
      if ((! betting_abstraction_.Asymmetric() &&
	   betting_abstraction_.AlwaysMinBet()) ||
	  (betting_abstraction_.Asymmetric() &&
	   player_acting == target_player &&
	   betting_abstraction_.OurAlwaysMinBet()) ||
	  (betting_abstraction_.Asymmetric() &&
	   player_acting != target_player &&
	   betting_abstraction_.OppAlwaysMinBet())) {
	// Allow a min bet
	unsigned int min_bet;
	if (num_street_bets == 0) {
	  min_bet = betting_abstraction_.MinBet();
	} else {
	  min_bet = last_bet_size;
	}
	unsigned int current_bet_to = current_pot_size / 2;
	unsigned int min_bet_pot_size = 2 * (current_bet_to + min_bet);
	if (min_bet_pot_size > (unsigned int)all_in_pot_size) {
	  pot_size_seen[all_in_pot_size] = true;
	} else {
	  pot_size_seen[min_bet_pot_size] = true;
	}
      }
    }

    double multiplier = 0;
    if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
      multiplier = betting_abstraction_.BetSizeMultiplier(street,
							  num_street_bets,
							  our_bet);
    }
    if (multiplier > 0) {
      int min_bet;
      if (num_street_bets == 0) {
	min_bet = betting_abstraction_.MinBet();
      } else {
	min_bet = last_bet_size;
      }
      int current_bet_to = current_pot_size / 2;
      int bet = min_bet;
      // We can add illegally small raise sizes here, but they get filtered
      // out in HandleBet().
      while (true) {
	int new_bet_to = current_bet_to + bet;
	int new_pot_size = 2 * new_bet_to;
	if (new_pot_size > all_in_pot_size) {
	  break;
	}
	if (betting_abstraction_.CloseToAllInFrac() > 0 &&
	    new_pot_size >=
	    all_in_pot_size * betting_abstraction_.CloseToAllInFrac()) {
	  break;
	}
	pot_size_seen[new_pot_size] = true;
	double d_bet = bet * multiplier;
	bet = (int)(d_bet + 0.5);
      }
      // Add all-in
      pot_size_seen[all_in_pot_size] = true;
    } else {
      // Not using multipliers
      bool no_regular_bets = 
	((! betting_abstraction_.Asymmetric() &&
	  current_pot_size >= betting_abstraction_.NoRegularBetThreshold()) ||
	 (betting_abstraction_.Asymmetric() &&
	  player_acting == target_player &&
	  current_pot_size >=
	  betting_abstraction_.OurNoRegularBetThreshold()) ||
	 (betting_abstraction_.Asymmetric() &&
	  player_acting != target_player &&
	  current_pot_size >=
	  betting_abstraction_.OppNoRegularBetThreshold()));

      if (! no_regular_bets &&
	  num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
	const vector<double> *pot_fracs =
	  betting_abstraction_.BetSizes(street, num_street_bets, our_bet);
	GetNewPotSizes(current_pot_size, *pot_fracs, player_acting,
		       target_player, pot_size_seen);
      }
    }
    for (int p = 0; p <= all_in_pot_size; ++p) {
      if (pot_size_seen[p]) {
	new_pot_sizes.push_back(p);
      }
    }  
    delete [] pot_size_seen;
  }

  unsigned int num_pot_sizes = new_pot_sizes.size();
  for (unsigned int i = 0; i < num_pot_sizes; ++i) {
    HandleBet(street, last_bet_size, current_pot_size, new_pot_sizes[i],
	      num_street_bets, player_acting, target_player, terminal_id,
	      bet_succs);
  }
}

shared_ptr<Node>
BettingTreeBuilder::CreateNoLimitSubtree(unsigned int street,
					 unsigned int pot_size,
					 unsigned int last_bet_size,
					 unsigned int num_street_bets,
					 unsigned int player_acting,
					 unsigned int target_player,
					 unsigned int *terminal_id) {
  shared_ptr<Node> call_succ(nullptr);
  shared_ptr<Node> fold_succ(nullptr);
  vector< shared_ptr<Node> > bet_succs;
  CreateNoLimitSuccs(street, pot_size, last_bet_size, num_street_bets,
		     player_acting, target_player, terminal_id, &call_succ,
		     &fold_succ, &bet_succs);
  if (call_succ == NULL && fold_succ == NULL && bet_succs.size() == 0) {
    fprintf(stderr, "Creating nonterminal with zero succs\n");
    fprintf(stderr, "This will cause problems\n");
    exit(-1);
  }
  // Assign nonterminal ID of kMaxUInt for now.
  shared_ptr<Node> node;
  node.reset(new Node(kMaxUInt, street, player_acting, call_succ, fold_succ,
		      &bet_succs, 255, pot_size));
  return node;
}

shared_ptr<Node>
BettingTreeBuilder::CreateNoLimitTree1(unsigned int street,
				       unsigned int pot_size,
				       unsigned int last_bet_size,
				       unsigned int num_street_bets,
				       unsigned int player_acting,
				       unsigned int target_player,
				       unsigned int *terminal_id) {
  return CreateNoLimitSubtree(street, pot_size, last_bet_size, num_street_bets,
			      player_acting, target_player, terminal_id);
}

