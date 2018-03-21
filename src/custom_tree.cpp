// For now, hard-coded to Li's proposed 10 big blind tree.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "betting_tree_builder.h"
#include "constants.h"
#include "game.h"
#include "split.h"

unsigned int
BettingTreeBuilder::GetBetSize(unsigned int st, unsigned int last_bet_to,
			       double pot_frac, unsigned int last_bet_size) {
  unsigned int pot_size = 2 * last_bet_to;
  unsigned int bet_size = (unsigned int)(pot_frac * pot_size + 0.5);
  unsigned int big_blind = Game::BigBlind();
  if (bet_size < big_blind) {
    bet_size = big_blind;
  }
  unsigned int all_in_bet_to = betting_abstraction_.StackSize();
  unsigned int new_bet_to = last_bet_to + bet_size;
  if (new_bet_to > all_in_bet_to) {
    new_bet_to = all_in_bet_to;
  }

  unsigned int new_pot_size = 2 * new_bet_to;
  unsigned int all_in_pot_size = 2 * all_in_bet_to;
  if (betting_abstraction_.CloseToAllInFrac() > 0 &&
      new_pot_size >=
      all_in_pot_size * betting_abstraction_.CloseToAllInFrac()) {
    new_bet_to = all_in_bet_to;
  }
    
  if (bet_size < last_bet_size && new_bet_to < all_in_bet_to) {
    // Minimum bet size is the last bet size (if any) - except for all-ins
    bet_size = last_bet_size;
  }
  return new_bet_to;
}

shared_ptr<Node>
BettingTreeBuilder::CreateCustomPostflopSubtree(unsigned int st,
						unsigned int player_acting,
						unsigned int last_bet_to,
						unsigned int last_bet_size,
						unsigned int num_previous_bets,
						unsigned int *terminal_id) {
  unsigned int all_in_bet_to = betting_abstraction_.StackSize();
  shared_ptr<Node> c, f;
  if (last_bet_size > 0 || player_acting != Game::FirstToAct(st)) {
    // Call of bet or check behind.  Create showdown node or subtree for
    // next street.
    if (st < 3) {
      c = CreateCustomPostflopSubtree(st + 1, Game::FirstToAct(st + 1),
				      last_bet_to, 0, 0, terminal_id);
    } else {
      c.reset(new Node((*terminal_id)++, st, 255, nullptr, nullptr, nullptr,
		       2, last_bet_to));
    }
  } else {
    // Open check
    c = CreateCustomPostflopSubtree(st, player_acting^1, last_bet_to, 0, 0,
				    terminal_id);
  }
  if (last_bet_size > 0) {
    unsigned int player_remaining = player_acting^1;
    // Player acting field encodes player remaining at fold nodes
    // bet_to - last_bet_size is how many chips the folder put in
    f.reset(new Node((*terminal_id)++, st, player_remaining, nullptr,
		     nullptr, nullptr, 1, last_bet_to - last_bet_size));
  }
  unique_ptr<bool []> bet_tos(new bool[all_in_bet_to + 1]);
  for (unsigned int i = 0; i <= all_in_bet_to; ++i) {
    bet_tos[i] = false;
  }
  if (betting_abstraction_.AlwaysAllIn()) {
    bet_tos[all_in_bet_to] = true;
  }
  if (num_previous_bets < 2) {
    unsigned int bet_to;
    bet_to = GetBetSize(st, last_bet_to, 0.5, last_bet_size);
    bet_tos[bet_to] = true;
    bet_to = GetBetSize(st, last_bet_to, 0.75, last_bet_size);
    bet_tos[bet_to] = true;
    bet_to = GetBetSize(st, last_bet_to, 1.0, last_bet_size);
    bet_tos[bet_to] = true;
  }
  vector< shared_ptr<Node> > bet_succs;
  for (unsigned int bet_to = last_bet_to + 1; bet_to <= all_in_bet_to;
       ++bet_to) {
    if (bet_tos[bet_to]) {
      unsigned int new_bet_size = bet_to - last_bet_to;
      // For bets we pass in the pot size without the pending bet included
      shared_ptr<Node> bet =
	CreateCustomPostflopSubtree(st, player_acting^1, bet_to, new_bet_size,
				    num_previous_bets + 1, terminal_id);
      bet_succs.push_back(bet);
    }
  }
  // Assign nonterminal ID of kMaxUInt for now.
  shared_ptr<Node> node;
  node.reset(new Node(kMaxUInt, st, player_acting, c, f, &bet_succs, 2,
		      last_bet_to));
  return node;
}

shared_ptr<Node>
BettingTreeBuilder::CreateCustomTree(unsigned int *terminal_id) {
  unsigned int small_blind = Game::SmallBlind();
  // One for each flop subtree
  shared_ptr<Node> cc, b1c, b2c, b1bc, cb1c, cb2c, cb3c, cb1bc, cb2bc;
  unsigned int player_acting = Game::FirstToAct(1);
  cc = CreateCustomPostflopSubtree(1, player_acting, 2 * small_blind, 0, 0,
				   terminal_id);
  b1c = CreateCustomPostflopSubtree(1, player_acting, 4 * small_blind, 0, 0,
				    terminal_id);
  b2c = CreateCustomPostflopSubtree(1, player_acting, 20 * small_blind, 0, 0,
				    terminal_id);
  b1bc = CreateCustomPostflopSubtree(1, player_acting, 20 * small_blind, 0, 0,
				     terminal_id);
  cb1c = CreateCustomPostflopSubtree(1, player_acting, 5 * small_blind, 0, 0,
				     terminal_id);
  cb2c = CreateCustomPostflopSubtree(1, player_acting, 8 * small_blind, 0, 0,
				     terminal_id);
  cb3c = CreateCustomPostflopSubtree(1, player_acting, 20 * small_blind, 0, 0,
				     terminal_id);
  cb1bc = CreateCustomPostflopSubtree(1, player_acting, 20 * small_blind, 0, 0,
				     terminal_id);
  cb2bc = CreateCustomPostflopSubtree(1, player_acting, 20 * small_blind, 0, 0,
				      terminal_id);

  shared_ptr<Node> f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
			      nullptr, 1, small_blind));
  shared_ptr<Node> b1f(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				nullptr, 1, 2 * small_blind));
  shared_ptr<Node> b2f(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				nullptr, 1, 2 * small_blind));
  shared_ptr<Node> b1bf(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				 nullptr, 1, 4 * small_blind));
  shared_ptr<Node> cb1f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				 nullptr, 1, 2 * small_blind));
  shared_ptr<Node> cb2f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				 nullptr, 1, 2 * small_blind));
  shared_ptr<Node> cb3f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				 nullptr, 1, 2 * small_blind));
  shared_ptr<Node> cb1bf(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				  nullptr, 1, 5 * small_blind));
  shared_ptr<Node> cb2bf(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				  nullptr, 1, 8 * small_blind));
  
  shared_ptr<Node> cb1b, cb2b, cb1, cb2, cb3, b1b, b1, b2, c, root;
  vector< shared_ptr<Node> > bet_succs;
  cb1b.reset(new Node(kMaxUInt, 0, 0, cb1bc, cb1bf, &bet_succs, 2,
		      20 * small_blind));
  bet_succs.clear();
  cb2b.reset(new Node(kMaxUInt, 0, 0, cb2bc, cb2bf, &bet_succs, 2,
		      20 * small_blind));
  bet_succs.clear();
  bet_succs.push_back(cb1b);
  cb1.reset(new Node(kMaxUInt, 0, 1, cb1c, cb1f, &bet_succs, 2,
		     5 * small_blind));
  bet_succs.clear();
  bet_succs.push_back(cb2b);
  cb2.reset(new Node(kMaxUInt, 0, 1, cb2c, cb2f, &bet_succs, 2,
		     8 * small_blind));
  bet_succs.clear();
  cb3.reset(new Node(kMaxUInt, 0, 1, cb3c, cb3f, &bet_succs, 2,
		     20 * small_blind));
  bet_succs.clear();
  b1b.reset(new Node(kMaxUInt, 0, 1, b1bc, b1bf, &bet_succs, 2,
		     20 * small_blind));
  bet_succs.clear();
  bet_succs.push_back(b1b);
  b1.reset(new Node(kMaxUInt, 0, 0, b1c, b1f, &bet_succs, 2,
		    4 * small_blind));
  bet_succs.clear();
  b2.reset(new Node(kMaxUInt, 0, 0, b2c, b2f, &bet_succs, 2,
		    20 * small_blind));
  bet_succs.clear();
  bet_succs.push_back(cb1);
  bet_succs.push_back(cb2);
  bet_succs.push_back(cb3);
  shared_ptr<Node> cf;
  c.reset(new Node(kMaxUInt, 0, 0, cc, cf, &bet_succs, 2, 2 * small_blind));
  bet_succs.clear();
  bet_succs.push_back(b1);
  bet_succs.push_back(b2);
  root.reset(new Node(kMaxUInt, 0, 1, c, f, &bet_succs, 2, 2 * small_blind));

  return root;
}

