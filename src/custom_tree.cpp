// For now, hard-coded to Li's proposed 10 big blind tree.
//
// Preflop (10BB):
//   Button can min-raise or all-in
//   After call, big-blind can raise to 2.5BB or 4BB.
//   Only raises are all-in
//
// Postflop (10BB):
//   Flop, turn and river all the same
//   In limped or mr pots I would like halfpot, ¾ pot and pot for bets and
//     raises.
//   In 2.5bb limp-raise pots, have 1.5bb, 3bb as betting options with min
//     raise as an option
//   In 4bb iso pots have 1.5bb as betting option with only all in as raise
//     size.
//   Finally- any postflop betsize or raise that puts in over 50% of a player’s
//     stack can be removed, and all in is always an option.
//
// Incorporates tweaks for 6-8BB and 12-13BB.
// From Li's e-mail:
//   -9bb and 11bb same as this 10bb model
//   -6,7,8bb could we have 2.25x and 3.5x vs limp preflop instead of 2.5x and
//     4x
//   -12, 13bb could we have 3x and 4.5x instead vs limp, and add also a 3bet
//     to 4bb vs 2bb min raise preflop (postflop sizes can be 2bb/all in on all
//     streets)
//
// Postflop in all situations have the same as the new 10bb model- except could
// you add min bet in position for MR pots for 12-13bb? VS the min bet, OOP can
// raise to halfpot/pot/1.5x pot/shove as before. This is because there should
// be a non-trivial MR range at those depths.
//
// After I asked for no 2.25x bet, Li specified:
// In this case can we have- 2x bet in the 6bb 7bb, and 2.5x bet in the 8bb
//   model?

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
					    const vector<double> &open_fracs,
					    const vector<double> &raise_fracs,
					    unsigned int *terminal_id) {
  unsigned int all_in_bet_to = betting_abstraction_.StackSize();
  shared_ptr<Node> c, f;
  if (last_bet_size > 0 || player_acting != Game::FirstToAct(st)) {
    // Call of bet or check behind.  Create showdown node or subtree for
    // next street.
    if (st < 3) {
      c = CreateCustomPostflopSubtree(st + 1, Game::FirstToAct(st + 1),
				      last_bet_to, 0, 0, open_fracs,
				      raise_fracs, terminal_id);
    } else {
      c.reset(new Node((*terminal_id)++, st, 255, nullptr, nullptr, nullptr,
		       2, last_bet_to));
    }
  } else {
    // Open check
    c = CreateCustomPostflopSubtree(st, player_acting^1, last_bet_to, 0, 0,
				    open_fracs, raise_fracs, terminal_id);
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
    const vector<double> &fracs = (num_previous_bets == 0 ? open_fracs :
				   raise_fracs);
    unsigned int num_fracs = fracs.size();
    for (unsigned int i = 0; i < num_fracs; ++i) {
      double frac = fracs[i];
      unsigned int bet_to;
      bet_to = GetBetSize(st, last_bet_to, frac, last_bet_size);
      bet_tos[bet_to] = true;
    }
  }
  vector< shared_ptr<Node> > bet_succs;
  for (unsigned int bet_to = last_bet_to + 1; bet_to <= all_in_bet_to;
       ++bet_to) {
    if (bet_tos[bet_to]) {
      unsigned int new_bet_size = bet_to - last_bet_to;
      // For bets we pass in the pot size without the pending bet included
      shared_ptr<Node> bet =
	CreateCustomPostflopSubtree(st, player_acting^1, bet_to, new_bet_size,
				    num_previous_bets + 1, open_fracs,
				    raise_fracs, terminal_id);
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
  shared_ptr<Node> cc, b1c, b2c, b1b1c, b1b2c, cb1c, cb2c, cb3c, cb1bc, cb2bc;
  shared_ptr<Node> b1b1bc;
  unsigned int player_acting = Game::FirstToAct(1);
  vector<double> no_open_fracs, no_raise_fracs;
  // Postflop options after cc or min-raise followed by a call
  vector<double> ccmr_open_fracs, ccmr_raise_fracs;
  // Postflop options after check, small bet and a call
  vector<double> cb1c_open_fracs, cb1c_raise_fracs;
  // Postflop options after check, larger bet and a call
  vector<double> cb2c_open_fracs, cb2c_raise_fracs;
  // Postflop options after min-bet, small raise and a call.
  vector<double> b1b1c_open_fracs, b1b1c_raise_fracs;

  unsigned int all_in_bet_to = betting_abstraction_.StackSize() * small_blind;
  unsigned int cb1_bet_to = 0, cb2_bet_to = 0;
  if (betting_abstraction_.StackSize() == 12 ||
      betting_abstraction_.StackSize() == 14) {
    cb1_bet_to = 4 * small_blind;
    cb2_bet_to = 7 * small_blind;
  } else if (betting_abstraction_.StackSize() == 16) {
    cb1_bet_to = 5 * small_blind;
    cb2_bet_to = 7 * small_blind;
  } else if (betting_abstraction_.StackSize() == 18 ||
	     betting_abstraction_.StackSize() == 20 ||
	     betting_abstraction_.StackSize() == 22) {
    cb1_bet_to = 5 * small_blind;
    cb2_bet_to = 8 * small_blind;
  } else if (betting_abstraction_.StackSize() == 24 ||
	     betting_abstraction_.StackSize() == 26) {
    cb1_bet_to = 6 * small_blind;
    cb2_bet_to = 9 * small_blind;
  } else if (betting_abstraction_.StackSize() == 28) {
    cb1_bet_to = 6 * small_blind;
    cb2_bet_to = 10 * small_blind;
  } else {
    fprintf(stderr, "Unhandled stack size: %u\n",
	    betting_abstraction_.StackSize());
    exit(-1);
  }
  bool small_3bet = betting_abstraction_.StackSize() >= 24;

  if (betting_abstraction_.StackSize() >= 24) {
    ccmr_open_fracs.push_back(0.25);
  }
  ccmr_open_fracs.push_back(0.5);
  ccmr_open_fracs.push_back(0.75);
  ccmr_open_fracs.push_back(1.0);
  ccmr_raise_fracs.push_back(0.5);
  ccmr_raise_fracs.push_back(0.75);
  ccmr_raise_fracs.push_back(1.0);
  cb1c_open_fracs.push_back(0.3);
  cb1c_open_fracs.push_back(0.6);
  cb1c_raise_fracs.push_back(0.1); // This will get rounded up to a min-raise
  cb2c_open_fracs.push_back(0.1875);
  b1b1c_open_fracs.push_back(0.125); // Should be 2BB into 16BB
  cc = CreateCustomPostflopSubtree(1, player_acting, 2 * small_blind, 0, 0,
				   ccmr_open_fracs, ccmr_raise_fracs,
				   terminal_id);
  b1c = CreateCustomPostflopSubtree(1, player_acting, 4 * small_blind, 0, 0,
				    ccmr_open_fracs, ccmr_raise_fracs,
				    terminal_id);
  b2c = CreateCustomPostflopSubtree(1, player_acting, all_in_bet_to, 0, 0,
				    no_open_fracs, no_raise_fracs,
				    terminal_id);
  b1b2c = CreateCustomPostflopSubtree(1, player_acting, all_in_bet_to, 0, 0,
				      no_open_fracs, no_raise_fracs,
				      terminal_id);
  if (small_3bet) {
    b1b1c = CreateCustomPostflopSubtree(1, player_acting, 8 * small_blind, 0,
					0, b1b1c_open_fracs, b1b1c_raise_fracs,
					terminal_id);
    b1b1bc = CreateCustomPostflopSubtree(1, player_acting, all_in_bet_to, 0,
					 0, no_open_fracs, no_raise_fracs,
					 terminal_id);
  }
  cb1c = CreateCustomPostflopSubtree(1, player_acting, cb1_bet_to, 0, 0,
				     cb1c_open_fracs, cb1c_raise_fracs,
				     terminal_id);
  cb2c = CreateCustomPostflopSubtree(1, player_acting, cb2_bet_to, 0, 0,
				     cb2c_open_fracs, cb2c_raise_fracs,
				     terminal_id);
  cb3c = CreateCustomPostflopSubtree(1, player_acting, all_in_bet_to, 0, 0,
				     no_open_fracs, no_raise_fracs,
				     terminal_id);
  cb1bc = CreateCustomPostflopSubtree(1, player_acting, all_in_bet_to, 0, 0,
				      no_open_fracs, no_raise_fracs,
				      terminal_id);
  cb2bc = CreateCustomPostflopSubtree(1, player_acting, all_in_bet_to, 0, 0,
				      no_open_fracs, no_raise_fracs,
				      terminal_id);

  // Fold nodes
  shared_ptr<Node> f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
			      nullptr, 1, small_blind));
  shared_ptr<Node> b1f(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				nullptr, 1, 2 * small_blind));
  shared_ptr<Node> b2f(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				nullptr, 1, 2 * small_blind));
  shared_ptr<Node> b1b2f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				  nullptr, 1, 4 * small_blind));
  shared_ptr<Node> b1b1f, b1b1bf;
  if (small_3bet) {
    b1b1f.reset(new Node((*terminal_id)++, 0, 0, nullptr, nullptr, nullptr, 1,
			 4 * small_blind));
    b1b1bf.reset(new Node((*terminal_id)++, 0, 0, nullptr, nullptr, nullptr, 1,
			  8 * small_blind));
    
  }
  shared_ptr<Node> cb1f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				 nullptr, 1, 2 * small_blind));
  shared_ptr<Node> cb2f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				 nullptr, 1, 2 * small_blind));
  shared_ptr<Node> cb3f(new Node((*terminal_id)++, 0, 0, nullptr, nullptr,
				 nullptr, 1, 2 * small_blind));
  shared_ptr<Node> cb1bf(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				  nullptr, 1, cb1_bet_to));
  shared_ptr<Node> cb2bf(new Node((*terminal_id)++, 0, 1, nullptr, nullptr,
				  nullptr, 1, cb2_bet_to));

  // Bet nodes
  shared_ptr<Node> cb1b, cb2b, cb1, cb2, cb3, b1b1, b1b2, b1, b2, c, root;
  vector< shared_ptr<Node> > bet_succs;
  cb1b.reset(new Node(kMaxUInt, 0, 0, cb1bc, cb1bf, &bet_succs, 2,
		      all_in_bet_to));
  bet_succs.clear();
  cb2b.reset(new Node(kMaxUInt, 0, 0, cb2bc, cb2bf, &bet_succs, 2,
		      all_in_bet_to));
  bet_succs.clear();
  bet_succs.push_back(cb1b);
  cb1.reset(new Node(kMaxUInt, 0, 1, cb1c, cb1f, &bet_succs, 2,
		     cb1_bet_to));
  bet_succs.clear();
  bet_succs.push_back(cb2b);
  cb2.reset(new Node(kMaxUInt, 0, 1, cb2c, cb2f, &bet_succs, 2,
		     cb2_bet_to));
  bet_succs.clear();
  cb3.reset(new Node(kMaxUInt, 0, 1, cb3c, cb3f, &bet_succs, 2,
		     all_in_bet_to));
  if (small_3bet) {
    shared_ptr<Node> b1b1b;
    bet_succs.clear();
    b1b1b.reset(new Node(kMaxUInt, 0, 0, b1b1bc, b1b1bf, &bet_succs, 2,
			 all_in_bet_to));
    bet_succs.clear();
    bet_succs.push_back(b1b1b);
    b1b1.reset(new Node(kMaxUInt, 0, 1, b1b1c, b1b1f, &bet_succs, 2,
			8 * small_blind));
  }
  bet_succs.clear();
  b1b2.reset(new Node(kMaxUInt, 0, 1, b1b2c, b1b2f, &bet_succs, 2,
		      all_in_bet_to));
  bet_succs.clear();
  if (small_3bet) {
    bet_succs.push_back(b1b1);
  }
  bet_succs.push_back(b1b2);
  b1.reset(new Node(kMaxUInt, 0, 0, b1c, b1f, &bet_succs, 2,
		    4 * small_blind));
  bet_succs.clear();
  b2.reset(new Node(kMaxUInt, 0, 0, b2c, b2f, &bet_succs, 2,
		    all_in_bet_to));
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

