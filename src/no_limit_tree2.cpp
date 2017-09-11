// The main novel feature of this approach is the way we chose the
// opponent bets.  See CreateNoLimitSuccs2().  We start with a minimum
// bet, then allow twice that, and twice that, and so forth.

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

void BettingTreeBuilder::GetNewPotSizes(int old_pot_size,
					const vector<int> &bet_amounts,
					unsigned int player_acting,
					unsigned int target_player,
					vector<int> *new_pot_sizes) {
  int all_in_pot_size = 2 * betting_abstraction_.StackSize();
  // Already all-in; no bets possible
  if (old_pot_size == all_in_pot_size) return;
  bool *pot_size_seen = new bool[all_in_pot_size + 1];
  for (int p = 0; p <= all_in_pot_size; ++p) {
    pot_size_seen[p] = false;
  }
  if ((! betting_abstraction_.Asymmetric() &&
       betting_abstraction_.AlwaysAllIn()) ||
      (betting_abstraction_.Asymmetric() && player_acting == target_player &&
       betting_abstraction_.AlwaysAllIn()) ||
      (betting_abstraction_.Asymmetric() && player_acting != target_player &&
       betting_abstraction_.OppAlwaysAllIn())) {
    // Allow an all-in bet
    pot_size_seen[all_in_pot_size] = true;
  }
  unsigned int num_bet_amounts = bet_amounts.size();
  for (unsigned int i = 0; i < num_bet_amounts; ++i) {
    int bet_size = bet_amounts[i];
    if (bet_size == 0) continue;
    int new_pot_size = old_pot_size + 2 * bet_size;
    if (new_pot_size > all_in_pot_size) {
      pot_size_seen[all_in_pot_size] = true;
    } else {
      pot_size_seen[new_pot_size] = true;
    }
  }
  for (int p = 0; p <= all_in_pot_size; ++p) {
    if (pot_size_seen[p]) {
      new_pot_sizes->push_back(p);
    }
  }  
  delete [] pot_size_seen;
}

// We are contemplating adding a bet.  We might or might not be facing a
// previous bet.  old_after_call_pot_size is the size of the pot after any
// pending bet is called.  new_after_call_pot_size is the size of the pot
// after the new bet we are considering is called.
// Note that pot sizes count money contributed by both players.  So if each
// player starts with 200 chips, max pot size is 400.
void BettingTreeBuilder::HandleBet2(unsigned int street,
				    unsigned int last_bet_size,
				    unsigned int old_after_call_pot_size,
				    unsigned int new_after_call_pot_size,
				    unsigned int num_street_bets,
				    unsigned int num_bets,
				    unsigned int player_acting,
				    unsigned int target_player,
				    unsigned int *terminal_id,
				    vector<Node *> *bet_succs) {
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
  Node *bet = CreateNoLimitSubtree2(street, old_after_call_pot_size,
				    new_bet_size, num_street_bets + 1,
				    num_bets + 1, player_acting,
				    player_acting^1, target_player,
				    terminal_id);
  bet_succs->push_back(bet);
}

// May return NULL
Node *BettingTreeBuilder::CreateCallSucc2(unsigned int street,
					  unsigned int last_pot_size,
					  unsigned int last_bet_size,
					  unsigned int num_street_bets,
					  unsigned int num_bets,
					  bool p1_last_bettor,
					  unsigned int player_acting,
					  unsigned int target_player,
					  unsigned int *terminal_id) {
  unsigned int after_call_pot_size = last_pot_size + 2 * last_bet_size;
  // We advance the street if we are calling a bet
  // Note that a call on the final street is considered to advance the street
  bool advance_street = num_street_bets > 0;
  // Check behind.  This assumes heads-up
  advance_street |= (Game::FirstToAct(street) != player_acting);
  Node *call_succ;
  unsigned int max_street = Game::MaxStreet();
  if (street < max_street && advance_street) {
    // Assume player 0 first to act postflop
    call_succ = CreateNoLimitSubtree2(street + 1, after_call_pot_size, 0, 0,
				      num_bets, p1_last_bettor, 0,
				      target_player, terminal_id);
  } else if (! advance_street) {
    // This is a check that does not advance the street
    call_succ = CreateNoLimitSubtree2(street, after_call_pot_size, 0, 0,
				      num_bets, p1_last_bettor,
				      player_acting^1, target_player,
				      terminal_id);
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
void BettingTreeBuilder::CreateNoLimitSuccs2(unsigned int street,
					     unsigned int last_pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_street_bets,
					     unsigned int num_bets,
					     bool p1_last_bettor,
					     unsigned int player_acting,
					     unsigned int target_player,
					     unsigned int *terminal_id,
					     Node **call_succ, Node **fold_succ,
					     vector<Node *> *bet_succs) {
  *fold_succ = NULL;
  bet_succs->clear();
  *call_succ = CreateCallSucc2(street, last_pot_size, last_bet_size,
			       num_street_bets, num_bets, p1_last_bettor,
			       player_acting, target_player, terminal_id);
  if (num_street_bets > 0 || (street == 0 && num_street_bets == 0 &&
			      Game::BigBlind() > Game::SmallBlind() &&
			      last_pot_size < 2 * Game::BigBlind())) {
    *fold_succ = CreateFoldSucc(street, last_pot_size, player_acting,
				terminal_id);
  }

  vector<int> new_pot_sizes;
  bool our_bet = (player_acting == target_player);
  unsigned int current_pot_size = last_pot_size + 2 * last_bet_size;
  if (our_bet) {
    if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
      const vector<double> *pot_fracs =
	betting_abstraction_.BetSizes(street, num_street_bets, our_bet);
      GetNewPotSizes(current_pot_size, *pot_fracs, player_acting,
		     target_player, &new_pot_sizes);
    }
  } else {
    vector<int> bet_sizes;
    int min_bet;
    if (num_street_bets == 0) {
      min_bet = betting_abstraction_.MinBet();
    } else {
      min_bet = last_bet_size;
    }
    int last_bet_to = last_pot_size / 2;
    int bet = min_bet;
    while (true) {
      bet_sizes.push_back(bet);
      bet *= 2;
      int bet_to = last_bet_to + bet;
      if (bet_to >= (int)betting_abstraction_.StackSize()) break;
    }
    GetNewPotSizes(current_pot_size, bet_sizes, player_acting, target_player,
		   &new_pot_sizes);
  }

  unsigned int num_pot_sizes = new_pot_sizes.size();
  for (unsigned int i = 0; i < num_pot_sizes; ++i) {
    HandleBet2(street, last_bet_size, current_pot_size, new_pot_sizes[i],
	       num_street_bets, num_bets, player_acting, target_player,
	       terminal_id, bet_succs);
  }
}

Node *BettingTreeBuilder::CreateNoLimitSubtree2(unsigned int street,
						unsigned int pot_size,
						unsigned int last_bet_size,
						unsigned int num_street_bets,
						unsigned int num_bets,
						bool p1_last_bettor,
						unsigned int player_acting,
						unsigned int target_player,
						unsigned int *terminal_id) {
  Node *call_succ = NULL, *fold_succ = NULL;
  vector<Node *> bet_succs;
  CreateNoLimitSuccs2(street, pot_size, last_bet_size, num_street_bets,
		      num_bets, p1_last_bettor, player_acting, target_player,
		      terminal_id, &call_succ, &fold_succ, &bet_succs);
  if (call_succ == NULL && fold_succ == NULL && bet_succs.size() == 0) {
    fprintf(stderr, "Creating nonterminal with zero succs\n");
    fprintf(stderr, "This will cause problems\n");
    exit(-1);
  }
  // Assign nonterminal ID of zero for now.  Will get updated at runtime after
  // tree is read from disk.
  Node *node = new Node(0, street, player_acting, call_succ, fold_succ,
			&bet_succs, 255, pot_size);
  return node;
}

Node *BettingTreeBuilder::CreateNoLimitTree2(unsigned int street,
					     unsigned int pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_street_bets,
					     unsigned int num_bets,
					     bool p1_last_bettor,
					     unsigned int player_acting,
					     unsigned int target_player,
					     unsigned int *terminal_id) {
  if (! betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Expect asymmetric betting abstraction\n");
    exit(-1);
  }
  return CreateNoLimitSubtree2(street, pot_size, last_bet_size, num_street_bets,
			       num_bets, p1_last_bettor, player_acting,
			       target_player, terminal_id);
}

