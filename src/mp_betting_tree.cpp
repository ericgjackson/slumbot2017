// I'll use a player index of 0 for the small blind and 1 for the big blind.
// Unfortunately, this is the opposite of what we did for heads-up.
//
// num_players_remaining is the number of players who have not folded.
// num_players_to_act basically tells you how many calls, checks or folds
// are needed until the action on the current street is over.  At the
// beginning of a betting round, it is initialized to the number of players
// remaining.  After a bet, it is re-initialized to the number of players
// remaining.  It gets decremented every time a player calls or folds.  When
// it reaches zero the action on the current street is complete.
//
// For multiplayer, we allow reentrancy for nodes within a street.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "betting_tree_builder.h"
#include "constants.h"
#include "fast_hash.h"
#include "game.h"
#include "split.h"

// Determine the next player to act, taking into account who has folded.
// Pass in first candidate for next player to act.
static unsigned int NextPlayerToAct(unsigned int p, bool *folded) {
  unsigned int num_players = Game::NumPlayers();
  while (folded[p]) {
    p = (p + 1) % num_players;
  }
  return p;
}

// Some additional tests get performed later.  For example, if a bet is a
// raise, we check that the raise size is valid (at least one big blind, at
// least as big as the previous bet).
void BettingTreeBuilder::GetNewBetTos(unsigned int old_bet_to,
				      const vector<double> &pot_fracs,
				      unsigned int player_acting,
				      unsigned int target_player,
				      bool *bet_to_seen) {
  unsigned int all_in_bet_to = betting_abstraction_.StackSize();
  // Already all-in; no bets possible
  if (old_bet_to == all_in_bet_to) return;
  unsigned int old_pot_size = 2 * old_bet_to;
  if ((! betting_abstraction_.Asymmetric() &&
       old_pot_size >= betting_abstraction_.OnlyPotThreshold()) ||
      (betting_abstraction_.Asymmetric() &&
       player_acting == target_player &&
       old_pot_size >=
       betting_abstraction_.OurOnlyPotThreshold()) ||
      (betting_abstraction_.Asymmetric() &&
       player_acting != target_player &&
       old_pot_size >=
       betting_abstraction_.OppOnlyPotThreshold())) {
    // Only pot-size bets allowed.  (Well, maybe all-ins also.)  So skip code
    // below that looks at pot_fracs.
    if (old_pot_size > 0) {
      unsigned int new_bet_to = 3 * old_pot_size / 2;
      if (new_bet_to > all_in_bet_to) {
	bet_to_seen[all_in_bet_to] = true;
      } else {
	bet_to_seen[new_bet_to] = true;
      }
    }
  } else {
    unsigned int num_pot_fracs = pot_fracs.size();
    for (unsigned int i = 0; i < num_pot_fracs; ++i) {
      double frac = pot_fracs[i];
      double double_bet_size = old_pot_size * frac;
      unsigned int bet_size = (unsigned int)(double_bet_size + 0.5);
      if (bet_size == 0) continue;
      unsigned int new_bet_to = old_bet_to + bet_size;
      unsigned int new_pot_size = 2 * new_bet_to;
      if (betting_abstraction_.CloseToAllInFrac() > 0 &&
	  new_pot_size >=
	  2 * all_in_bet_to * betting_abstraction_.CloseToAllInFrac()) {
	// Don't add the bet, but add an all-in bet instead
	bet_to_seen[all_in_bet_to] = true;
      } else if (new_bet_to > all_in_bet_to) {
	bet_to_seen[all_in_bet_to] = true;
      } else {
	bet_to_seen[new_bet_to] = true;
      }
    }
  }
}

shared_ptr<Node>
BettingTreeBuilder::CreateMPFoldSucc(unsigned int street,
				     unsigned int last_bet_size,
				     unsigned int bet_to,
				     unsigned int num_street_bets,
				     unsigned int player_acting,
				     unsigned int num_players_to_act,
				     bool *folded, unsigned int target_player,
				     string *key, unsigned int *terminal_id) {
  shared_ptr<Node> fold_succ;
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_players_remaining = 0;
  for (unsigned int p = 0; p < num_players; ++p) {
    if (! folded[p]) ++num_players_remaining;
  }
  if (num_players_remaining <= 1) {
    fprintf(stderr, "CreateMPFoldSucc npr %u?!?\n", num_players_remaining);
    exit(-1);
  }
  string new_key = *key;
  if (betting_abstraction_.BettingKey(street)) {
    AddStringToKey("f", &new_key);
  }
  if (num_players_remaining == 2) {
    // This fold completes the hand
    unsigned int p;
    for (p = 0; p < num_players; ++p) {
      if (! folded[p] && p != player_acting) break;
    }
    if (p == num_players) {
      fprintf(stderr, "Everyone folded?!?\n");
      fprintf(stderr, "street %u\n", street);
      fprintf(stderr, "num_players_to_act %u\n", num_players_to_act);
      exit(-1);
    }
    // Note: bet_to passed in includes final uncalled bet.  So we can't use
    // bet_to field to determine counterfactual value at fold node in CFR.
    fold_succ.reset(new Node((*terminal_id)++, street, p, nullptr,
			     nullptr, nullptr, 1, bet_to));
  } else if (num_players_to_act == 1 && street == max_street) {
    // Showdown
    fold_succ.reset(new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			     nullptr, num_players_remaining - 1, bet_to));
  } else {
    // Hand is not over
    unique_ptr<bool []> new_folded(new bool[num_players]);
    for (unsigned int p = 0; p < num_players; ++p) {
      new_folded[p] = folded[p];
    }
    new_folded[player_acting] = true;
    if (num_players_to_act == 1) {
      // This fold completes the street
      fold_succ = CreateMPStreet(street + 1, bet_to, new_folded.get(),
				 target_player, &new_key, terminal_id);
    } else {
      // This is a fold that does not end the street
      unsigned int next_player_to_act =
	NextPlayerToAct((player_acting + 1) % num_players, new_folded.get());
      fold_succ = CreateMPSubtree(street, last_bet_size, bet_to,
				  num_street_bets, next_player_to_act,
				  num_players_to_act - 1, new_folded.get(),
				  target_player, &new_key, terminal_id);
    }
  }
  return fold_succ;
}

// num_players_to_act is (re)initialized when a player bets.  It gets
// decremented every time a player calls or folds.  When it reaches zero
// the action on the current street is complete.
shared_ptr<Node>
BettingTreeBuilder::CreateMPCallSucc(unsigned int street,
				     unsigned int last_bet_size,
				     unsigned int bet_to,
				     unsigned int num_street_bets,
				     unsigned int player_acting,
				     unsigned int num_players_to_act,
				     bool *folded, unsigned int target_player,
				     string *key, unsigned int *terminal_id) {
  bool advance_street = (num_players_to_act == 1);
  shared_ptr<Node> call_succ;
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_players_remaining = 0;
  for (unsigned int p = 0; p < num_players; ++p) {
    if (! folded[p]) ++num_players_remaining;
  }
  if (folded[player_acting]) {
    fprintf(stderr, "CreateMPCallSucc: Player already folded\n");
    exit(-1);
  }
  if (num_players_to_act == 0) exit(-1);
  if (num_players_to_act > 1000000) exit(-1);
  if (num_players_to_act > num_players_remaining) exit(-1);
  string new_key = *key;
  if (betting_abstraction_.BettingKey(street)) {
    AddStringToKey("c", &new_key);
  }
  if (street < max_street && advance_street) {
    // Call completes action on current street.
    call_succ = CreateMPStreet(street + 1, bet_to, folded, target_player,
			       &new_key, terminal_id);
  } else if (! advance_street) {
    // This is a check or call that does not advance the street
    unsigned int next_player_to_act =
      NextPlayerToAct((player_acting + 1) % num_players, folded);
    // Shouldn't happen
    if (next_player_to_act == player_acting) exit(-1);
    call_succ = CreateMPSubtree(street, last_bet_size, bet_to, num_street_bets,
				next_player_to_act, num_players_to_act - 1,
				folded, target_player, &new_key, terminal_id);
  } else {
    // This is a call on the final street
    call_succ.reset(new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			     nullptr, num_players_remaining, bet_to));

  }
  return call_succ;
}

// We are contemplating adding a bet.  We might or might not be facing a
// previous bet.  old_after_call_pot_size is the size of the pot after any
// pending bet is called.  new_after_call_pot_size is the size of the pot
// after the new bet we are considering is called.
// Note that pot sizes count money contributed by both players.  So if each
// player starts with 200 chips, max pot size is 400.
void BettingTreeBuilder::MPHandleBet(unsigned int street,
				     unsigned int last_bet_size,
				     unsigned int last_bet_to,
				     unsigned int new_bet_to,
				     unsigned int num_street_bets,
				     unsigned int player_acting,
				     unsigned int num_players_to_act,
				     bool *folded, unsigned int target_player,
				     string *key, unsigned int *terminal_id,
				     vector< shared_ptr<Node> > *bet_succs) {
  // New bet must be of size greater than zero
  if (new_bet_to <= last_bet_to) return;

  unsigned int new_bet_size = new_bet_to - last_bet_to;

  bool all_in_bet = (new_bet_to == betting_abstraction_.StackSize());

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

  string new_key = *key;
  if (betting_abstraction_.BettingKey(street)) {
    AddStringToKey("b", &new_key);
    AddUnsignedIntToKey(new_bet_size, &new_key);
  }
  
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_players_remaining = 0;
  for (unsigned int p = 0; p < num_players; ++p) {
    if (! folded[p]) ++num_players_remaining;
  }
  unsigned int next_player_to_act =
    NextPlayerToAct((player_acting + 1) % num_players, folded);

  // Shouldn't happen
  if (num_players_remaining == 1) {
    fprintf(stderr, "Only one player remaining after bet?!?\n");
    exit(-1);
  }
  // For bets we pass in the pot size without the pending bet included
  shared_ptr<Node> bet =
    CreateMPSubtree(street, new_bet_size, new_bet_to, num_street_bets + 1,
		    next_player_to_act, num_players_remaining - 1, folded,
		    target_player, &new_key, terminal_id);
  bet_succs->push_back(bet);
}

// last_bet_size is the *size* of the last bet.  Needed to ensure raises
// are legal.  bet_to is what the last bet was *to*.
void BettingTreeBuilder::CreateMPSuccs(unsigned int street,
				       unsigned int last_bet_size,
				       unsigned int bet_to,
				       unsigned int num_street_bets,
				       unsigned int player_acting,
				       unsigned int num_players_to_act,
				       bool *folded,
				       unsigned int target_player, string *key, 
				       unsigned int *terminal_id,
				       shared_ptr<Node> *call_succ,
				       shared_ptr<Node> *fold_succ,
				       vector< shared_ptr<Node> > *
				       bet_succs) {
  if (folded[player_acting]) {
    fprintf(stderr, "CreateMPSuccs: Player already folded\n");
    exit(-1);
  }
  bet_succs->clear();
  *call_succ = CreateMPCallSucc(street, last_bet_size, bet_to,
				num_street_bets, player_acting,
				num_players_to_act, folded,
				target_player, key, terminal_id);
  // Allow fold if num_street_bets > 0 OR this is the very first action of
  // the hand (i.e., the small blind can open fold).
  // Preflop you can fold when num_street_bets is zero if
  bool can_fold = (num_street_bets > 0);
  if (! can_fold && street == 0) {
    // Special case for the preflop.  When num_street_bets is zero, everyone
    // except the big blind can still fold.  The big blind is the player
    // prior to the player who is first to act.
    unsigned int fta = Game::FirstToAct(0);
    unsigned int bb;
    if (fta == 0) bb = Game::NumPlayers() - 1;
    else          bb = fta - 1;
    can_fold = (player_acting != bb);
  }
  if (can_fold) {
    *fold_succ = CreateMPFoldSucc(street, last_bet_size, bet_to,
				  num_street_bets, player_acting,
				  num_players_to_act, folded, target_player,
				  key, terminal_id);
  }

  bool our_bet = (target_player == player_acting);
  unsigned int all_in_bet_to = betting_abstraction_.StackSize();
  bool *bet_to_seen = new bool[all_in_bet_to + 1];
  for (unsigned int bt = 0; bt <= all_in_bet_to; ++bt) {
    bet_to_seen[bt] = false;
  }
  
  if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
    if ((! betting_abstraction_.Asymmetric() &&
	 betting_abstraction_.AlwaysAllIn()) ||
	(betting_abstraction_.Asymmetric() && our_bet &&
	 betting_abstraction_.OurAlwaysAllIn()) ||
	(betting_abstraction_.Asymmetric() && ! our_bet &&
	 betting_abstraction_.OppAlwaysAllIn())) {
      // Allow an all-in bet
      bet_to_seen[all_in_bet_to] = true;
    }
  }
  
  if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
    if ((! betting_abstraction_.Asymmetric() &&
	 betting_abstraction_.AlwaysMinBet(street, num_street_bets)) ||
	(betting_abstraction_.Asymmetric() && our_bet &&
	 betting_abstraction_.OurAlwaysMinBet(street, num_street_bets)) ||
	(betting_abstraction_.Asymmetric() && ! our_bet &&
	 betting_abstraction_.OppAlwaysMinBet(street, num_street_bets))) {
      // Allow a min bet
      unsigned int min_bet;
      if (num_street_bets == 0) {
	min_bet = betting_abstraction_.MinBet();
      } else {
	min_bet = last_bet_size;
      }
      unsigned int new_bet_to = bet_to + min_bet;
      if (new_bet_to > all_in_bet_to) {
	bet_to_seen[all_in_bet_to] = true;
      } else {
	bet_to_seen[all_in_bet_to] = true;
      }
    }
  }
  
  if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
    const vector<double> *pot_fracs =
      betting_abstraction_.BetSizes(street, num_street_bets, our_bet);
    GetNewBetTos(bet_to, *pot_fracs, player_acting, target_player,
		 bet_to_seen);
  }
  vector<unsigned int> new_bet_tos;
  for (unsigned int bt = 0; bt <= all_in_bet_to; ++bt) {
    if (bet_to_seen[bt]) {
      new_bet_tos.push_back(bt);
    }
  }  
  delete [] bet_to_seen;

  unsigned int num_bet_tos = new_bet_tos.size();
  for (unsigned int i = 0; i < num_bet_tos; ++i) {
    MPHandleBet(street, last_bet_size, bet_to, new_bet_tos[i], num_street_bets,
		player_acting, num_players_to_act, folded, target_player, key,
		terminal_id, bet_succs);
  }
}

shared_ptr<Node>
BettingTreeBuilder::CreateMPSubtree(unsigned int street,
				    unsigned int last_bet_size,
				    unsigned int bet_to,
				    unsigned int num_street_bets,
				    unsigned int player_acting,
				    unsigned int num_players_to_act,
				    bool *folded, unsigned int target_player,
				    string *key, unsigned int *terminal_id) {
  if (folded[player_acting]) {
    fprintf(stderr, "CreateMPSubtree: Player already folded\n");
    exit(-1);
  }
  string final_key;
  if (betting_abstraction_.ReentrantStreet(street) &&
      2 * bet_to >= betting_abstraction_.MinReentrantPot()) {
    // As it stands, we don't encode which players have folded.  But we do
    // encode num_players_to_act.
    unsigned int num_players = Game::NumPlayers();
    unsigned int num_players_remaining = 0;
    for (unsigned int p = 0; p < num_players; ++p) {
      if (! folded[p]) ++num_players_remaining;
    }
    final_key = *key;
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(street, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(player_acting, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(bet_to, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(last_bet_size, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(num_players_remaining, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(num_players_to_act, &final_key);
    // fprintf(stderr, "Key: %s\n", final_key.c_str());
    shared_ptr<Node> node;
    if (FindReentrantNode(final_key, &node)) {
      return node;
    }
  }
  shared_ptr<Node> call_succ(nullptr);
  shared_ptr<Node> fold_succ(nullptr);
  vector< shared_ptr<Node> > bet_succs;
  CreateMPSuccs(street, last_bet_size, bet_to, num_street_bets,
		player_acting, num_players_to_act, folded, target_player, key,
		terminal_id, &call_succ, &fold_succ, &bet_succs);
  if (call_succ == NULL && fold_succ == NULL && bet_succs.size() == 0) {
    fprintf(stderr, "Creating nonterminal with zero succs\n");
    fprintf(stderr, "This will cause problems\n");
    exit(-1);
  }
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_players_remaining = 0;
  for (unsigned int p = 0; p < num_players; ++p) {
    if (! folded[p]) ++num_players_remaining;
  }
  // Assign nonterminal ID of kMaxUInt for now.
  shared_ptr<Node> node;
  node.reset(new Node(kMaxUInt, street, player_acting, call_succ, fold_succ,
		      &bet_succs, num_players_remaining, bet_to));
  if (betting_abstraction_.ReentrantStreet(street) &&
      2 * bet_to >= betting_abstraction_.MinReentrantPot()) {
    AddReentrantNode(final_key, node);
  }
  return node;
}

// Not called for the root
shared_ptr<Node>
BettingTreeBuilder::CreateMPStreet(unsigned int street, unsigned int bet_to,
				   bool *folded, unsigned int target_player,
				   string *key, unsigned int *terminal_id) {
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_players_remaining = 0;
  for (unsigned int p = 0; p < num_players; ++p) {
    if (! folded[p]) ++num_players_remaining;
  }
  unsigned int next_player_to_act =
    NextPlayerToAct(Game::FirstToAct(street + 1), folded);
  shared_ptr<Node> node =
    CreateMPSubtree(street, 0, bet_to, 0, next_player_to_act,
		    num_players_remaining, folded, target_player, key,
		    terminal_id);
  return node;
}

shared_ptr<Node>
BettingTreeBuilder::CreateMPTree(unsigned int target_player,
				 unsigned int *terminal_id) {
  unsigned int initial_street = betting_abstraction_.InitialStreet();
  unsigned int player_acting = Game::FirstToAct(initial_street_);
  unsigned int initial_bet_to = Game::BigBlind();
  unsigned int last_bet_size = Game::BigBlind() - Game::SmallBlind();
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> folded(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    folded[p] = false;
  }
  string key;
  return CreateMPSubtree(initial_street, last_bet_size, initial_bet_to, 0,
			 player_acting, Game::NumPlayers(), folded.get(),
			 target_player, &key, terminal_id);
}

