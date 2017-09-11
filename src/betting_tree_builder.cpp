#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "betting_tree_builder.h"
#include "files.h"
#include "game.h"
#include "io.h"
// #include "pool.h"

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
					  Node **call_succ, Node **fold_succ,
					  vector<Node *> *bet_succs) {
  *call_succ = NULL;
  *fold_succ = NULL;
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
    *call_succ = CreateLimitSubtree(street + 1, new_pot_size, 0, 0, kMaxUInt,
				    Game::FirstToAct(street + 1), terminal_id);
  } else if (! advance_street) {
    // Check that does not advance street
    *call_succ = CreateLimitSubtree(street, new_pot_size, 0, 0,
				    last_bettor, next_player, terminal_id);
  } else {
    *call_succ = new Node((*terminal_id)++, street, 255, nullptr,
			  nullptr, nullptr, 255, new_pot_size);
  }
  if (num_bets > 0 || (street == 0 && num_bets == 0 &&
		       Game::BigBlind() > Game::SmallBlind() &&
		       pot_size < 2 * Game::BigBlind())) {
    *fold_succ = new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			  nullptr, player_acting, pot_size);
  }

  if (num_bets == max_bets) return;

  // For now, hard-code to limit holdem bet sizes
  // int new_bet_size = betting_abstraction_.BetSize(street);
  int new_bet_size;
  if (street <= 1) new_bet_size = 2;
  else             new_bet_size = 4;

  Node *bet = CreateLimitSubtree(street, new_pot_size, new_bet_size,
				 num_bets + 1, player_acting, next_player,
				 terminal_id);
  bet_succs->push_back(bet);
}

// Only called for limit trees
// Assumes one granularity
Node *BettingTreeBuilder::CreateLimitSubtree(unsigned int street,
					     unsigned int pot_size,
					     unsigned int last_bet_size,
					     unsigned int num_bets,
					     unsigned int last_bettor,
					     unsigned int player_acting,
					     unsigned int *terminal_id) {
  Node *call_succ, *fold_succ;
  vector<Node *> bet_succs;
  CreateLimitSuccs(street, pot_size, last_bet_size, num_bets, last_bettor,
		   player_acting, terminal_id, &call_succ, &fold_succ,
		   &bet_succs);
  unsigned int nt_id = 0;
  // Assign nonterminal ID of zero for now.  Will get updated at runtime after
  // tree is read from disk.
  Node *node = new Node(nt_id, street, player_acting, call_succ, fold_succ,
			&bet_succs, 255, pot_size);

  return node;
}

void BettingTreeBuilder::Build(void) {
  unsigned int initial_pot_size = 2 * Game::SmallBlind() + 2 * Game::Ante();
  unsigned int last_bet_size = Game::BigBlind() - Game::SmallBlind();
  unsigned int terminal_id = 0;
  unsigned int player_acting = Game::FirstToAct(initial_street_);

  if (betting_abstraction_.Limit()) {
    root_ = CreateLimitSubtree(initial_street_, initial_pot_size,
			       last_bet_size, 0, kMaxUInt, player_acting,
			       &terminal_id);
  } else {
    if (betting_abstraction_.NoLimitTreeType() == 0) {
    } else if (betting_abstraction_.NoLimitTreeType() == 1) {
      root_ = CreateNoLimitTree1(initial_street_, initial_pot_size,
				 last_bet_size, 0, player_acting,
				 target_player_, &terminal_id);
    } else if (betting_abstraction_.NoLimitTreeType() == 3) {
      root_ = CreateNoLimitTree3(initial_street_, initial_pot_size,
				 last_bet_size, 0, false, player_acting,
				 target_player_, &terminal_id);
    }
  }
  num_terminals_ = terminal_id;
}

void BettingTreeBuilder::Write(Node *node, Writer *writer) {
  writer->WriteUnsignedInt(node->ID());
  writer->WriteUnsignedShort(node->PotSize());
  writer->WriteUnsignedShort(node->NumSuccs());
  writer->WriteUnsignedShort(node->Flags());
  writer->WriteUnsignedChar(node->PlayerActing());
  writer->WriteUnsignedChar(node->PlayerFolding());
  if (node->Terminal()) {
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Write(node->IthSucc(s), writer);
  }
}

void BettingTreeBuilder::Write(void) {
  char buf[500];
  if (asymmetric_) {
    sprintf(buf, "%s/betting_tree.%s.%s.%u", Files::StaticBase(),
	    Game::GameName().c_str(),
	    betting_abstraction_.BettingAbstractionName().c_str(),
	    target_player_);
  } else {
    sprintf(buf, "%s/betting_tree.%s.%s", Files::StaticBase(),
	    Game::GameName().c_str(),
	    betting_abstraction_.BettingAbstractionName().c_str());
  }
  Writer writer(buf);
  Write(root_, &writer);
}


void BettingTreeBuilder::Initialize(void) {
  initial_street_ = betting_abstraction_.InitialStreet();
  stack_size_ = betting_abstraction_.StackSize();
  all_in_pot_size_ = 2 * stack_size_;
  min_bet_ = betting_abstraction_.MinBet();

  // pool_ = new Pool();
  root_ = NULL;
  num_terminals_ = 0;
}

BettingTreeBuilder::BettingTreeBuilder(const BettingAbstraction &ba) :
  betting_abstraction_(ba) {
  asymmetric_ = false;
  // Parameter should be ignored for symmetric trees.
  target_player_ = kMaxUInt;
  Initialize();
}

BettingTreeBuilder::BettingTreeBuilder(const BettingAbstraction &ba,
				       unsigned int target_player) :
  betting_abstraction_(ba) {
  asymmetric_ = true;
  target_player_ = target_player;
  Initialize();
}

