// Allows creation of a no-limit tree that includes an additional bet size
// that was not in the original abstraction.  We create the entire tree for
// the street on which the additional bet is added (and any subsequent
// streets).
//
// I guess we'll assume that the new bet size is actually new.
//
// Should I allow *multiple* new bet sizes?

#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "betting_tree_builder.h"

Node *BettingTreeBuilder::BuildAugmented(Node *base_node, Node *branch_point,
					 unsigned int new_bet_size,
					 unsigned int num_street_bets,
					 unsigned int target_player,
					 unsigned int *num_terminals) {
  Node *new_node;
  if (base_node->Terminal()) {
    new_node = new Node(base_node);
    // Need to reindex the terminal nodes
    new_node->SetTerminalID(*num_terminals);
    ++*num_terminals;
  } else if (base_node == branch_point) {
    unsigned int old_num_succs = base_node->NumSuccs();
    Node *call_succ = nullptr, *fold_succ = nullptr;
    if (base_node->HasCallSucc()) {
      call_succ = BuildAugmented(base_node->IthSucc(base_node->CallSuccIndex()),
				 nullptr, 0, num_street_bets, target_player,
				 num_terminals);
    }
    if (base_node->HasFoldSucc()) {
      fold_succ = BuildAugmented(base_node->IthSucc(base_node->FoldSuccIndex()),
				 nullptr, 0, num_street_bets, target_player,
				 num_terminals);
    }
    vector<Node *> bet_succs;
    unsigned int last_bet_size = 0;
    for (unsigned int s = 0; s < old_num_succs; ++s) {
      if (s == base_node->CallSuccIndex() || s == base_node->FoldSuccIndex()) {
	continue;
      }
      Node *b = base_node->IthSucc(s);
      Node *bc = b->IthSucc(b->CallSuccIndex());
      unsigned int bet_size = (bc->PotSize() - base_node->PotSize()) / 2;
      if (new_bet_size > last_bet_size && new_bet_size < bet_size) {
	unsigned int pot_size;
	if (num_street_bets > 0) {
	  // Bet pending
	  Node *c = base_node->IthSucc(base_node->CallSuccIndex());
	  pot_size = c->PotSize();
	} else {
	  pot_size = base_node->PotSize();
	}
	// Hang on, I don't have enough information at this point
	Node *new_bet = CreateNoLimitSubtree(base_node->Street(), pot_size,
					     new_bet_size, num_street_bets + 1,
					     base_node->PlayerActing()^1,
					     target_player, num_terminals);
	bet_succs.push_back(new_bet);
      }
      Node *new_bet = BuildAugmented(b, nullptr, 0, num_street_bets + 1,
				     target_player, num_terminals);
      bet_succs.push_back(new_bet);
      last_bet_size = bet_size;
    }
    new_node = new Node(0, base_node->Street(),
			base_node->PlayerActing(), call_succ, fold_succ,
			&bet_succs, base_node->PlayerFolding(),
			base_node->PotSize());
  } else {
    new_node = new Node(base_node);
    unsigned int num_succs = base_node->NumSuccs();
    for (unsigned int s = 0; s < num_succs; ++s) {
      unsigned int new_num_street_bets =
	(s == base_node->CallSuccIndex() || s == base_node->FoldSuccIndex()) ?
	num_street_bets : num_street_bets + 1;
      Node *new_succ = BuildAugmented(base_node->IthSucc(s), branch_point,
				      new_bet_size, new_num_street_bets,
				      target_player, num_terminals);
      new_node->SetIthSucc(s, new_succ);
    }
  }
  return new_node;
}

#if 0
static bool GetPath(Node *node, Node *target, vector<Node *> *rev_path) {
  if (node->Terminal()) return false;
  if (node == target) {
    rev_path->push_back(node);
    return true;
  }
  if (node->Street() > target->Street()) return false;
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (GetPath(node->IthSucc(s), target, rev_path)) {
      base_path->push_back(node);
      return true;
    }
  }
  return false;
}

Node *BettingTreeBuilder::AddBet(Node *street_root, Node *branch_point,
				 unsigned int new_bet_size) {
  vector<Node *> rev_path;
  if (! GetPath(street_root, branch_point, &rev_path)) {
    fprintf(stderr, "Couldn't find path from street root to branch point\n");
    exit(-1);
  }
}
#endif
