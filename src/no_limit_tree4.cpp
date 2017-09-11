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

// Could rule out pot size of 380 when all_in_pot_size_ is 400.
// There need to be some additional tests performed on the pot sizes.  For
// example, if these new pot sizes correspond to raises we need to check
// that the raise size is valid (at least one big blind, at least as big
// as the previous bet).
void BettingTreeBuilder::GetNewPotSizes4(unsigned int old_pot_size,
					 const vector<double> &pot_fracs,
					 unsigned int player_acting,
					 unsigned int target_player,
					 const BettingAbstraction &
					 betting_abstraction,
					 vector<int> *new_pot_sizes) {
  // Already all-in; no bets possible
  if (old_pot_size == all_in_pot_size_) return;
  bool *pot_size_seen = new bool[all_in_pot_size_ + 1];
  for (unsigned int p = 0; p <= all_in_pot_size_; ++p) {
    pot_size_seen[p] = false;
  }
  if ((! betting_abstraction.Asymmetric() &&
       betting_abstraction.AlwaysAllIn()) ||
      (betting_abstraction.Asymmetric() && player_acting == target_player &&
       betting_abstraction.AlwaysAllIn()) ||
      (betting_abstraction.Asymmetric() && player_acting != target_player &&
       betting_abstraction.OppAlwaysAllIn())) {
    // Allow an all-in bet
    pot_size_seen[all_in_pot_size_] = true;
  }

  
  unsigned int num_pot_fracs = pot_fracs.size();
  for (unsigned int i = 0; i < num_pot_fracs; ++i) {
    double frac = pot_fracs[i];
    double double_bet_size = old_pot_size * frac;
    unsigned int bet_size = (unsigned int)(double_bet_size + 0.5);
    if (bet_size == 0) continue;
    unsigned int new_pot_size = old_pot_size + 2 * bet_size;
    if (new_pot_size > all_in_pot_size_) {
      pot_size_seen[all_in_pot_size_] = true;
    } else {
      pot_size_seen[new_pot_size] = true;
    }
  }
  
  for (unsigned int p = 0; p <= all_in_pot_size_; ++p) {
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
void BettingTreeBuilder::HandleBet4(unsigned int street,
				    unsigned int last_bet_size,
				    unsigned int old_after_call_pot_size,
				    unsigned int new_after_call_pot_size,
				    unsigned int num_street_bets,
				    unsigned int player_acting,
				    unsigned int target_player,
				    const BettingAbstraction &
				    immediate_betting_abstraction,
				    const BettingAbstraction &
				    future_betting_abstraction,
				    unsigned int *terminal_id,
				    vector<Node *> *bet_succs) {
  // Obviously pot size can't go down after additional bet
  if (new_after_call_pot_size <= old_after_call_pot_size) return;

  // Integer division is appropriate here because these "after call"
  // pot sizes are always even (since each player has put an equal amount
  // of money into the pot).
  unsigned int new_bet_size =
    (new_after_call_pot_size - old_after_call_pot_size) / 2;

  bool all_in_bet = new_after_call_pot_size == all_in_pot_size_;

  // Cannot make a bet that is smaller than the min bet (usually the big
  // blind)
  if (new_bet_size < min_bet_ && ! all_in_bet) {
    return;
  }

  // In general, cannot make a bet that is smaller than the previous
  // bet size.  Exception is that you can always raise all-in
  if (new_bet_size < last_bet_size && ! all_in_bet) {
    return;
  }

  // For bets we pass in the pot size without the pending bet included
  Node *bet = CreateNoLimitSubtree4(street, old_after_call_pot_size,
				    new_bet_size, num_street_bets + 1,
				    player_acting^1, target_player,
				    future_betting_abstraction,
				    future_betting_abstraction,
				    terminal_id);
  bet_succs->push_back(bet);
}

// May return NULL
Node *BettingTreeBuilder::CreateCallSucc4(unsigned int street,
					  unsigned int last_pot_size,
					  unsigned int last_bet_size,
					  unsigned int num_street_bets,
					  unsigned int player_acting,
					  unsigned int target_player,
					  const BettingAbstraction &
					  immediate_betting_abstraction,
					  const BettingAbstraction &
					  future_betting_abstraction,
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
    call_succ = CreateNoLimitSubtree4(street + 1, after_call_pot_size, 0, 0,
				      Game::FirstToAct(street + 1),
				      target_player,
				      future_betting_abstraction,
				      future_betting_abstraction, terminal_id);
  } else if (! advance_street) {
    // This is a check that does not advance the street
    call_succ = CreateNoLimitSubtree4(street, after_call_pot_size, 0, 0,
				      player_acting^1, target_player,
				      future_betting_abstraction,
				      future_betting_abstraction,
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
void BettingTreeBuilder::CreateNoLimitSuccs4(unsigned int street,
					     unsigned int last_pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_street_bets,
					     unsigned int player_acting,
					     unsigned int target_player,
					     const BettingAbstraction &
					     immediate_betting_abstraction,
					     const BettingAbstraction &
					     future_betting_abstraction,
					     unsigned int *terminal_id,
					     Node **call_succ,
					     Node **fold_succ,
					     vector<Node *> *bet_succs) {
  *fold_succ = NULL;
  *call_succ = NULL;
  bet_succs->clear();
  bool no_open_limp = 
    ((! immediate_betting_abstraction.Asymmetric() &&
      immediate_betting_abstraction.NoOpenLimp()) ||
     (immediate_betting_abstraction.Asymmetric() &&
      player_acting == target_player &&
      immediate_betting_abstraction.OurNoOpenLimp()) ||
     (immediate_betting_abstraction.Asymmetric() &&
      player_acting != target_player &&
      immediate_betting_abstraction.OppNoOpenLimp()));
  if (! (street == 0 && num_street_bets == 0 &&
	 player_acting == Game::FirstToAct(0) && no_open_limp)) {
    *call_succ = CreateCallSucc4(street, last_pot_size, last_bet_size,
				 num_street_bets, player_acting, target_player,
				 immediate_betting_abstraction,
				 future_betting_abstraction, terminal_id);
  }
  if (num_street_bets > 0 || (street == 0 && num_street_bets == 0 &&
			      Game::BigBlind() > Game::SmallBlind() &&
			      last_pot_size < 2 * Game::BigBlind())) {
    *fold_succ = CreateFoldSucc(street, last_pot_size, player_acting,
				terminal_id);
  }

  unsigned int current_pot_size = last_pot_size + 2 * last_bet_size;
  vector<int> new_pot_sizes;
  bool our_bet = (target_player == player_acting);
  if (num_street_bets <
      immediate_betting_abstraction.MaxBets(street, our_bet)) {
    const vector<double> *pot_fracs =
      immediate_betting_abstraction.BetSizes(street, num_street_bets, our_bet);
    GetNewPotSizes4(current_pot_size, *pot_fracs, player_acting,
		    target_player, immediate_betting_abstraction,
		    &new_pot_sizes);
  }

  unsigned int num_pot_sizes = new_pot_sizes.size();
  for (unsigned int i = 0; i < num_pot_sizes; ++i) {
    HandleBet4(street, last_bet_size, current_pot_size, new_pot_sizes[i],
	       num_street_bets, player_acting, target_player,
	       immediate_betting_abstraction, future_betting_abstraction,
	       terminal_id, bet_succs);
  }
}

Node *BettingTreeBuilder::CreateNoLimitSubtree4(unsigned int street,
						unsigned int pot_size,
						unsigned int last_bet_size,
						unsigned int num_street_bets,
						unsigned int player_acting,
						unsigned int target_player,
						const BettingAbstraction &
						immediate_betting_abstraction,
						const BettingAbstraction &
						future_betting_abstraction,
						unsigned int *terminal_id) {
  Node *call_succ = NULL, *fold_succ = NULL;
  vector<Node *> bet_succs;
  CreateNoLimitSuccs4(street, pot_size, last_bet_size, num_street_bets,
		      player_acting, target_player,
		      immediate_betting_abstraction,
		      future_betting_abstraction,
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

Node *BettingTreeBuilder::CreateNoLimitTree4(unsigned int street,
					     unsigned int pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_street_bets,
					     unsigned int player_acting,
					     unsigned int target_player,
					     const BettingAbstraction &
					     immediate_betting_abstraction,
					     const BettingAbstraction &
					     future_betting_abstraction,
					     unsigned int *terminal_id) {
  return CreateNoLimitSubtree4(street, pot_size, last_bet_size,
			       num_street_bets, player_acting, target_player,
			       immediate_betting_abstraction,
			       future_betting_abstraction, terminal_id);
}
