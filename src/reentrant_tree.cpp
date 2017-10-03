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

// uint64_t fasthash64(const void *buf, size_t len, uint64_t seed);

static void AddStringToKey(const string &s, string *key) {
  *key += s;
}

static void AddUnsignedIntToKey(unsigned int ui, string *key) {
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
				    unsigned int last_pot_size,
				    unsigned int last_bet_size,
				    unsigned int num_street_bets,
				    unsigned int player_acting,
				    unsigned int target_player,
				    string *key, unsigned int *terminal_id) {
  unsigned int after_call_pot_size = last_pot_size + 2 * last_bet_size;
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
    call_succ = CreateReentrantStreet(street + 1, after_call_pot_size,
				      target_player, &new_key, terminal_id);
  } else if (! advance_street) {
    // This is a check that does not advance the street
    call_succ = RCreateNoLimitSubtree(street, after_call_pot_size, 0, 0,
				      player_acting^1, target_player,
				      &new_key, terminal_id);
  } else {
    // This is a call on the final street
    call_succ.reset(new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			     nullptr, 255, after_call_pot_size));

  }
  return call_succ;
}

// We are contemplating adding a bet.  We might or might not be facing a
// previous bet.  old_after_call_pot_size is the size of the pot after any
// pending bet is called.  new_after_call_pot_size is the size of the pot
// after the new bet we are considering is called.
// Note that pot sizes count money contributed by both players.  So if each
// player starts with 200 chips, max pot size is 400.
void BettingTreeBuilder::RHandleBet(unsigned int street,
				    unsigned int last_bet_size,
				    unsigned int old_after_call_pot_size,
				    unsigned int new_after_call_pot_size,
				    unsigned int num_street_bets,
				    unsigned int player_acting,
				    unsigned int target_player,
				    string *key,
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

  string new_key = *key;
  if (betting_abstraction_.BettingKey(street)) {
    AddStringToKey("b", &new_key);
    AddUnsignedIntToKey(new_bet_size, &new_key);
  }
  
  // For bets we pass in the pot size without the pending bet included
  shared_ptr<Node> bet =
    RCreateNoLimitSubtree(street, old_after_call_pot_size, new_bet_size,
			  num_street_bets + 1, player_acting^1, target_player,
			  &new_key, terminal_id);
  bet_succs->push_back(bet);
}

// There are several pot sizes here which is confusing.  When we call
// CreateNoLimitSuccs() it may be as the result of adding a bet action.
// So we have the pot size before that bet, the pot size after that bet,
// and potentially the pot size after a raise of the bet.  last_pot_size is
// the first pot size - i.e., the pot size before the pending bet.
// current_pot_size is the pot size after the pending bet.
void BettingTreeBuilder::RCreateNoLimitSuccs(unsigned int street,
					     unsigned int last_pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_street_bets,
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
    *call_succ = RCreateCallSucc(street, last_pot_size, last_bet_size,
				 num_street_bets, player_acting, target_player,
				 key, terminal_id);
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
	   betting_abstraction_.AlwaysMinBet(street, num_street_bets)) ||
	  (betting_abstraction_.Asymmetric() &&
	   player_acting == target_player &&
	   betting_abstraction_.OurAlwaysMinBet(street, num_street_bets)) ||
	  (betting_abstraction_.Asymmetric() &&
	   player_acting != target_player &&
	   betting_abstraction_.OppAlwaysMinBet(street, num_street_bets))) {
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
    RHandleBet(street, last_bet_size, current_pot_size, new_pot_sizes[i],
	       num_street_bets, player_acting, target_player, key, terminal_id,
	       bet_succs);
  }
}

shared_ptr<Node>
BettingTreeBuilder::RCreateNoLimitSubtree(unsigned int street,
					  unsigned int pot_size,
					  unsigned int last_bet_size,
					  unsigned int num_street_bets,
					  unsigned int player_acting,
					  unsigned int target_player,
					  string *key,
					  unsigned int *terminal_id) {
  shared_ptr<Node> call_succ(nullptr);
  shared_ptr<Node> fold_succ(nullptr);
  vector< shared_ptr<Node> > bet_succs;
  RCreateNoLimitSuccs(street, pot_size, last_bet_size, num_street_bets,
		      player_acting, target_player, key, terminal_id,
		      &call_succ, &fold_succ, &bet_succs);
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

// Not called for the root
shared_ptr<Node>
BettingTreeBuilder::CreateReentrantStreet(unsigned int street,
					  unsigned int pot_size,
					  unsigned int target_player,
					  string *key,
					  unsigned int *terminal_id) {
  if (street == Game::MaxStreet() &&
      pot_size >= betting_abstraction_.MinReentrantPot()) {
    AddUnsignedIntToKey(pot_size, key);
#if 0
    if (pot_size == 54) {
      fprintf(stderr, "Key: %s\n", key->c_str());
    }
#endif
    shared_ptr<Node> node;
    if (FindReentrantNode(*key, &node)) {
      return node;
    }
  }
  shared_ptr<Node> node =
    RCreateNoLimitSubtree(street, pot_size, 0, 0, Game::FirstToAct(street),
			  target_player, key, terminal_id);
  AddReentrantNode(*key, node);
  return node;
}

shared_ptr<Node>
BettingTreeBuilder::CreateNoLimitTree2(unsigned int target_player,
				       unsigned int *terminal_id) {
  unsigned int initial_street = betting_abstraction_.InitialStreet();
  unsigned int player_acting = Game::FirstToAct(initial_street_);
  unsigned int initial_pot_size = 2 * Game::SmallBlind() + 2 * Game::Ante();
  unsigned int last_bet_size = Game::BigBlind() - Game::SmallBlind();
  
  string key;
  return RCreateNoLimitSubtree(initial_street, initial_pot_size, last_bet_size,
			       0, player_acting, target_player, &key,
			       terminal_id);
}
