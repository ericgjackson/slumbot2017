// When we create a node, see if an equivalent node already exists.
// We construct a key with the important attributes of the node.  Could
// be pot size.  Could be some part of the previous betting sequence.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "betting_abstraction.h"
#include "betting_tree_builder.h"
#include "betting_tree.h"
#include "fast_hash.h"
#include "game.h"

using namespace std;

void AddStringToKey(const string &s, string *key) {
  *key += s;
}

void AddUnsignedIntToKey(unsigned int ui, string *key) {
  char buf[10];
  sprintf(buf, "%u", ui);
  *key += buf;
}

unsigned long long int HashKey(const string &key) {
  return fasthash64((void *)key.c_str(), key.size(), 0);
}

bool BettingTreeBuilder::FindReentrantNode(const string &key,
					   shared_ptr<Node> *node) {
  unordered_map< unsigned long long int, shared_ptr<Node> >::iterator it;
  unsigned long long int h = HashKey(key);
  it = node_map_->find(h);
  if (it != node_map_->end()) {
    *node = it->second;
    return true;
  } else {
    return false;
  }
}

void BettingTreeBuilder::AddReentrantNode(const string &key,
					  shared_ptr<Node> node) {
  unsigned long long int h = HashKey(key);
  (*node_map_)[h] = node;
}

// May return NULL
shared_ptr<Node>
BettingTreeBuilder::RCreateCallSucc(unsigned int street,
				    unsigned int last_bet_size,
				    unsigned int bet_to,
				    unsigned int num_street_bets,
				    unsigned int num_bets,
				    unsigned int last_aggressor,
				    unsigned int player_acting,
				    unsigned int target_player,
				    string *key, unsigned int *terminal_id) {
  string new_key = *key;
  if (betting_abstraction_.BettingKey(street)) {
    AddStringToKey("c", &new_key);
  }
  // We advance the street if we are calling a bet
  // Note that a call on the final street is considered to advance the street
  bool advance_street = num_street_bets > 0;
  // This assumes heads-up
  advance_street |= (Game::FirstToAct(street) != player_acting);
  shared_ptr<Node> call_succ;
  unsigned int max_street = Game::MaxStreet();
  if (street < max_street && advance_street) {
    call_succ = CreateReentrantStreet(street + 1, bet_to, num_bets,
				      last_aggressor, target_player, &new_key,
				      terminal_id);
  } else if (! advance_street) {
    // This is a check that does not advance the street
    call_succ = RCreateNoLimitSubtree(street, 0, bet_to, num_street_bets,
				      num_bets, last_aggressor,
				      player_acting^1, target_player, &new_key,
				      terminal_id);
  } else {
    // This is a call on the final street
    call_succ.reset(new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			     nullptr, 2, bet_to));

  }
  return call_succ;
}

// We are contemplating adding a bet.  We might or might not be facing a
// previous bet.
void BettingTreeBuilder::RHandleBet(unsigned int street,
				    unsigned int last_bet_size,
				    unsigned int last_bet_to,
				    unsigned int new_bet_to,
				    unsigned int num_street_bets,
				    unsigned int num_bets,
				    unsigned int player_acting,
				    unsigned int target_player, string *key,
				    unsigned int *terminal_id,
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
  
  // For bets we pass in the pot size without the pending bet included
  shared_ptr<Node> bet =
    RCreateNoLimitSubtree(street, new_bet_size, new_bet_to,
			  num_street_bets + 1, num_bets + 1, player_acting,
			  player_acting^1, target_player, &new_key,
			  terminal_id);
  bet_succs->push_back(bet);
}

// There are several pot sizes here which is confusing.  When we call
// CreateNoLimitSuccs() it may be as the result of adding a bet action.
void BettingTreeBuilder::RCreateNoLimitSuccs(unsigned int street,
					     unsigned int last_bet_size,
					     unsigned int bet_to,
					     unsigned int num_street_bets,
					     unsigned int num_bets,
					     unsigned int last_aggressor,
					     unsigned int player_acting,
					     unsigned int target_player,
					     string *key,
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
    *call_succ = RCreateCallSucc(street, last_bet_size, bet_to,
				 num_street_bets, num_bets, last_aggressor,
				 player_acting, target_player, key,
				 terminal_id);
  }
  bool can_fold = (num_street_bets > 0);
  if (! can_fold && street == 0) {
    // Special case for the preflop.  When num_street_bets is zero, the small
    // blind can still fold.
    can_fold = (player_acting == Game::FirstToAct(0));
  }
  if (can_fold) {
    *fold_succ = CreateFoldSucc(street, last_bet_size, bet_to, player_acting,
				terminal_id);
  }

  bool our_bet = (target_player == player_acting);
#if 0
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
  }
#endif

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
      // Should check if allowable.
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
	if (betting_abstraction_.AllowableBetTo(new_bet_to)) {
	  bet_to_seen[new_bet_to] = true;
	} else {
	  unsigned int old_pot_size = 2 * bet_to;
	  unsigned int nearest_allowable_bet_to =
	    NearestAllowableBetTo(old_pot_size, new_bet_to, last_bet_size);
	  bet_to_seen[nearest_allowable_bet_to] = true;
#if 0
	  if (nearest_allowable_bet_to != new_bet_to) {
	    fprintf(stderr, "Changed %u to %u\n", new_bet_to - bet_to,
		    nearest_allowable_bet_to - bet_to);
	  }
#endif
	}
      }
    }
  }

  if (num_street_bets < betting_abstraction_.MaxBets(street, our_bet)) {
    const vector<double> *pot_fracs =
      betting_abstraction_.BetSizes(street, num_street_bets, our_bet,
				    player_acting);
    GetNewBetTos(bet_to, last_bet_size, *pot_fracs, player_acting,
		 target_player, bet_to_seen);
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
    RHandleBet(street, last_bet_size, bet_to, new_bet_tos[i], num_street_bets,
	       num_bets, player_acting, target_player, key, terminal_id,
	       bet_succs);
  }
}

shared_ptr<Node>
BettingTreeBuilder::RCreateNoLimitSubtree(unsigned int st,
					  unsigned int last_bet_size,
					  unsigned int bet_to,
					  unsigned int num_street_bets,
					  unsigned int num_bets,
					  unsigned int last_aggressor,
					  unsigned int player_acting,
					  unsigned int target_player,
					  string *key,
					  unsigned int *terminal_id) {
  string final_key;
  bool merge = false;
  if (betting_abstraction_.ReentrantStreet(st) &&
      2 * bet_to >= betting_abstraction_.MinReentrantPot() &&
      num_bets >= betting_abstraction_.MinReentrantBets(st, 2)) {
    merge = true;
    final_key = *key;
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(st, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(player_acting, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(num_street_bets, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(bet_to, &final_key);
    AddStringToKey(":", &final_key);
    AddUnsignedIntToKey(last_bet_size, &final_key);
    if (betting_abstraction_.LastAggressorKey()) {
      AddStringToKey(":", &final_key);
      AddUnsignedIntToKey(last_aggressor, &final_key);
    }
    // fprintf(stderr, "Key: %s\n", final_key.c_str());
    shared_ptr<Node> node;
    if (FindReentrantNode(final_key, &node)) {
      return node;
    }
  }
  shared_ptr<Node> call_succ(nullptr);
  shared_ptr<Node> fold_succ(nullptr);
  vector< shared_ptr<Node> > bet_succs;
  RCreateNoLimitSuccs(st, last_bet_size, bet_to, num_street_bets, num_bets,
		      last_aggressor, player_acting, target_player, key,
		      terminal_id, &call_succ, &fold_succ, &bet_succs);
  if (call_succ == NULL && fold_succ == NULL && bet_succs.size() == 0) {
    fprintf(stderr, "Creating nonterminal with zero succs\n");
    fprintf(stderr, "This will cause problems\n");
    exit(-1);
  }
  // Assign nonterminal ID of kMaxUInt for now.
  shared_ptr<Node> node;
  node.reset(new Node(kMaxUInt, st, player_acting, call_succ, fold_succ,
		      &bet_succs, 2, bet_to));
  if (merge) {
    AddReentrantNode(final_key, node);
  }
  return node;
}

// Not called for the root
shared_ptr<Node>
BettingTreeBuilder::CreateReentrantStreet(unsigned int street,
					  unsigned int bet_to,
					  unsigned int num_bets,
					  unsigned int last_aggressor,
					  unsigned int target_player,
					  string *key,
					  unsigned int *terminal_id) {
  shared_ptr<Node> node =
    RCreateNoLimitSubtree(street, 0, bet_to, 0, num_bets, last_aggressor,
			  Game::FirstToAct(street), target_player, key,
			  terminal_id);
  return node;
}

shared_ptr<Node>
BettingTreeBuilder::CreateNoLimitTree2(unsigned int target_player,
				       unsigned int *terminal_id) {
  unsigned int initial_street = betting_abstraction_.InitialStreet();
  unsigned int player_acting = Game::FirstToAct(initial_street_);
  unsigned int initial_bet_to = Game::BigBlind();
  unsigned int last_bet_size = Game::BigBlind() - Game::SmallBlind();

  string key;
  // Make the big blind the last aggressor.
  return RCreateNoLimitSubtree(initial_street, last_bet_size, initial_bet_to,
			       0, 0, player_acting^1, player_acting,
			       target_player, &key, terminal_id);
}
