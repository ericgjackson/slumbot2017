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
			      nullptr, nullptr, 255, new_pot_size));
  }
  if (num_bets > 0 || (street == 0 && num_bets == 0 &&
		       Game::BigBlind() > Game::SmallBlind() &&
		       pot_size < 2 * Game::BigBlind())) {
    fold_succ->reset(new Node((*terminal_id)++, street, 255, nullptr, nullptr,
			      nullptr, player_acting, pot_size));
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
				 fold_succ, &bet_succs, 255, pot_size));

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
    } else if (betting_abstraction_.NoLimitTreeType() == 2) {
      root_ = CreateNoLimitTree2(target_player_, &terminal_id);
#if 0
    } else if (betting_abstraction_.NoLimitTreeType() == 3) {
      root_ = CreateNoLimitTree3(initial_street_, initial_pot_size,
				 last_bet_size, 0, false, player_acting,
				 target_player_, &terminal_id);
#endif
    }
  }
  num_terminals_ = terminal_id;
}

// To handle reentrant trees we keep a boolean array of nodes that have
// already been visited.  Note that even if a node has already been visited,
// we still write out the properties of the node (ID, flags, pot size, etc.).
// But we prevent ourselves from redundantly writing out the subtree below
// the node more than once.
void BettingTreeBuilder::Write(Node *node, unsigned int **num_nonterminals,
			       Writer *writer) {
  unsigned int st = node->Street();
  unsigned int id = node->ID();
  unsigned int pa = node->PlayerActing();
  bool nt_first_seen = (id == kMaxUInt);
  // Assign IDs during writing
  if (nt_first_seen) {
    id = num_nonterminals[pa][st]++;
    node->SetNonterminalID(id);
  }
  writer->WriteUnsignedInt(id);
  writer->WriteUnsignedShort(node->PotSize());
  writer->WriteUnsignedShort(node->NumSuccs());
  writer->WriteUnsignedShort(node->Flags());
  writer->WriteUnsignedChar(pa);
  writer->WriteUnsignedChar(node->PlayerFolding());
  if (node->Terminal()) {
    return;
  }
  if (! nt_first_seen) return;
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Write(node->IthSucc(s), num_nonterminals, writer);
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
  
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  unsigned int **num_nonterminals = new unsigned int *[num_players];
  for (unsigned int pa = 0; pa <= 1; ++pa) {
    num_nonterminals[pa] = new unsigned int[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      num_nonterminals[pa][st] = 0;
    }
  }
  
  Writer writer(buf);
  Write(root_.get(), num_nonterminals, &writer);
  for (unsigned int pa = 0; pa <= 1; ++pa) {
    delete [] num_nonterminals[pa];
  }
  delete [] num_nonterminals;
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
  node_map_.reset(new
		  unordered_map< unsigned long long int, shared_ptr<Node> >);
    
  Initialize();
}

BettingTreeBuilder::BettingTreeBuilder(const BettingAbstraction &ba,
				       unsigned int target_player) :
  betting_abstraction_(ba) {
  asymmetric_ = true;
  target_player_ = target_player;
  Initialize();
}

