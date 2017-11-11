#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_tree_builder.h"
#include "betting_tree.h"
#include "constants.h"
#include "game.h"

// Only called for limit trees currently.  Could replace vector of bet succs
// with a single bet succ.
// Only supports head-up limit at the moment
void BettingTreeBuilder::CreateLimitSuccs(unsigned int street,
					  unsigned int pot_size,
					  unsigned int last_bet_size,
					  unsigned int num_bets,
					  unsigned int last_bettor,
					  unsigned int player_acting,
					  unsigned int *terminal_id,
					  shared_ptr<Node> *call_succ,
					  shared_ptr<Node> *fold_succ,
					  vector< shared_ptr<Node> >
					  *bet_succs) {
  // *call_succ = NULL;
  // *fold_succ = NULL;
  bet_succs->clear();
  // For now, hard-code to full size of limit holdem
  unsigned int max_bets;
  if (street == 0) {
    // max_bets = betting_abstraction_.MaxPreflopBets();
    max_bets = 3;
  } else {
    // max_bets = betting_abstraction_.MaxPostflopBets();
    max_bets = 4;
  }
  unsigned int new_pot_size = pot_size + 2 * last_bet_size;
  unsigned int next_player = (player_acting + 1) % Game::NumPlayers();
  bool advance_street = num_bets > 0 && next_player == last_bettor;
  if (num_bets == 0 && next_player == Game::FirstToAct(street)) {
    // Checked around (or, preflop, calls and checks)
    advance_street = true;
  }
  unsigned int max_street = Game::MaxStreet();
  if (street < max_street && advance_street) {
    *call_succ = CreateLimitSubtree(street + 1, new_pot_size, 0, 0,
				    kMaxUInt, Game::FirstToAct(street + 1),
				    terminal_id);
  } else if (! advance_street) {
    // Check that does not advance street
    *call_succ = CreateLimitSubtree(street, new_pot_size, 0, 0, last_bettor,
				    next_player, terminal_id);
  } else {
    call_succ->reset(new Node((*terminal_id)++, street, 255, nullptr,
			      nullptr, nullptr, 2, new_pot_size));
  }
  if (num_bets > 0 || (street == 0 && num_bets == 0 &&
		       Game::BigBlind() > Game::SmallBlind() &&
		       pot_size < 2 * Game::BigBlind())) {
    fold_succ->reset(new Node((*terminal_id)++, street, player_acting^1,
			      nullptr, nullptr, nullptr, 1, pot_size));
  }

  if (num_bets == max_bets) return;

  // For now, hard-code to limit holdem bet sizes
  // int new_bet_size = betting_abstraction_.BetSize(street);
  int new_bet_size;
  if (street <= 1) new_bet_size = 2;
  else             new_bet_size = 4;

  shared_ptr<Node> bet = CreateLimitSubtree(street, new_pot_size, new_bet_size,
					    num_bets + 1, player_acting,
					    next_player, terminal_id);
  bet_succs->push_back(bet);
}

// Only called for limit trees
// Assumes one granularity
shared_ptr<Node>
BettingTreeBuilder::CreateLimitSubtree(unsigned int street,
				       unsigned int pot_size,
				       unsigned int last_bet_size,
				       unsigned int num_bets,
				       unsigned int last_bettor,
				       unsigned int player_acting,
				       unsigned int *terminal_id) {
  // Node *call_succ, *fold_succ;
  // vector<Node *> bet_succs;
  shared_ptr<Node> call_succ(nullptr);
  shared_ptr<Node> fold_succ(nullptr);
  vector< shared_ptr<Node> > bet_succs;
  CreateLimitSuccs(street, pot_size, last_bet_size, num_bets, last_bettor,
		   player_acting, terminal_id, &call_succ, &fold_succ,
		   &bet_succs);
  // Assign nonterminal ID of kMaxUInt for now.
  shared_ptr<Node> node(new Node(kMaxUInt, street, player_acting, call_succ,
				 fold_succ, &bet_succs, 2, pot_size));

  return node;
}

shared_ptr<Node>
BettingTreeBuilder::CreateLimitTree(unsigned int *terminal_id) {
  unsigned int initial_street = betting_abstraction_.InitialStreet();
  unsigned int initial_pot_size = 2 * Game::SmallBlind() + 2 * Game::Ante();
  unsigned int last_bet_size = Game::BigBlind() - Game::SmallBlind();
  unsigned int player_acting = Game::FirstToAct(initial_street_);
  
  return CreateLimitSubtree(initial_street, initial_pot_size, last_bet_size,
			    0, kMaxUInt, player_acting, terminal_id);
}

