#include <stdio.h>
#include <stdlib.h>

#include "betting_tree.h"
#include "card_abstraction.h"
#include "game.h"
#include "nonterminal_ids.h"

static void AssignNonterminalIDs(Node *node, unsigned int **num_nonterminals) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  unsigned int p = node->PlayerActing();
  node->SetNonterminalID(num_nonterminals[p][st]);
  num_nonterminals[p][st] += 1;
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    AssignNonterminalIDs(node->IthSucc(s), num_nonterminals);
  }
}

// Can pass in NULL for ret_num_nonterminals if you don't want them
void AssignNonterminalIDs(Node *root, unsigned int ***ret_num_nonterminals) {
  unsigned int **num_nonterminals = new unsigned int *[2];
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int p = 0; p <= 1; ++p) {
    num_nonterminals[p] = new unsigned int[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      num_nonterminals[p][st] = 0;
    }
  }
  AssignNonterminalIDs(root, num_nonterminals);
  if (ret_num_nonterminals) {
    *ret_num_nonterminals = num_nonterminals;
  } else {
    for (unsigned int p = 0; p <= 1; ++p) {
      delete [] num_nonterminals[p];
    }
    delete [] num_nonterminals;
  }
}

// Can pass in NULL for ret_num_nonterminals if you don't want them
void AssignNonterminalIDs(BettingTree *betting_tree,
			  unsigned int ***ret_num_nonterminals) {
  AssignNonterminalIDs(betting_tree->Root(), ret_num_nonterminals);
}

// Handles reentrancy.  Assumes nonterminal IDs have been assigned densely,
// and that they account for reentrancy.
static void CountNumNonterminals(Node *node, unsigned int **num_nonterminals) {
  if (node->Terminal()) return;
  unsigned int pa = node->PlayerActing();
  unsigned int st = node->Street();
  unsigned int nt_id = node->NonterminalID();
  if (nt_id >= num_nonterminals[pa][st]) {
    num_nonterminals[pa][st] = nt_id + 1;
  } else {
    // Should only see lower nonterminal ID in case of reentrancy.
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    CountNumNonterminals(node->IthSucc(s), num_nonterminals);
  }
}

unsigned int **CountNumNonterminals(Node *root) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  unsigned int **num_nonterminals = new unsigned int *[num_players];
  for (unsigned int pa = 0; pa < num_players; ++pa) {
    num_nonterminals[pa] = new unsigned int[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      num_nonterminals[pa][st] = 0;
    }
  }
  CountNumNonterminals(root, num_nonterminals);
  return num_nonterminals;
}

unsigned int **CountNumNonterminals(BettingTree *betting_tree) {
  return CountNumNonterminals(betting_tree->Root());
}

