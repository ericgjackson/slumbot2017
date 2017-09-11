#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "io.h"
#include "nonterminal_ids.h"
// #include "pool.h"

using namespace std;

Node::Node(unsigned int id, unsigned int street, unsigned int player_acting,
	   Node *call_succ, Node *fold_succ, vector<Node *> *bet_succs,
	   unsigned int player_folding, unsigned int pot_size) {
  unsigned int num_succs = 0;
  if (call_succ) {
    ++num_succs;
  }
  if (fold_succ) {
    ++num_succs;
  }
  unsigned int num_bet_succs = 0;
  if (bet_succs)  {
    num_bet_succs = bet_succs->size();
    num_succs += num_bet_succs;
  }
  if (num_succs == 0) {
    succs_ = NULL;
  } else {
    // succs_ = (Node **)pool->Allocate(num_succs, sizeof(void *));
    succs_ = new Node *[num_succs];
    unsigned int i = 0;
    if (call_succ) succs_[i++] = call_succ;
    if (fold_succ) succs_[i++] = fold_succ;
    for (unsigned int j = 0; j < num_bet_succs; ++j) {
      succs_[i++] = (*bet_succs)[j];
    }
  }
  id_ = id;
  pot_size_ = pot_size;
  num_succs_ = num_succs;
  flags_ = 0;
  if (call_succ)   flags_ |= kHasCallSuccFlag;
  if (fold_succ)   flags_ |= kHasFoldSuccFlag;
  flags_ |= (((unsigned short)street) << kStreetShift);
  if (player_acting > 255) {
    fprintf(stderr, "player_acting OOB: %u\n", player_acting);
    exit(-1);
  }
  if (player_folding > 255) {
    fprintf(stderr, "player_folding OOB: %u\n", player_folding);
    exit(-1);
  }
  player_acting_ = player_acting;
  player_folding_ = player_folding;
}

Node::Node(Node *src) {
  unsigned int num_succs = src->NumSuccs();
  if (num_succs == 0) {
    succs_ = NULL;
  } else {
    // succs_ = (Node **)pool->Allocate(num_succs, sizeof(void *));
    succs_ = new Node *[num_succs];
  }
  for (unsigned int s = 0; s < num_succs; ++s) succs_[s] = NULL;
  id_ = src->id_;
  pot_size_ = src->pot_size_;
  num_succs_ = src->num_succs_;
  flags_ = src->flags_;
  player_acting_ = src->player_acting_;
  player_folding_ = src->player_folding_;
}

Node::Node(unsigned int id, unsigned int pot_size, unsigned int num_succs,
	   unsigned short flags, unsigned char player_acting,
	   unsigned char player_folding) {
  id_ = id;
  pot_size_ = pot_size;
  num_succs_ = num_succs;
  if (num_succs == 0) {
    succs_ = NULL;
  } else {
    // succs_ = (Node **)pool->Allocate(num_succs, sizeof(void *));
    succs_ = new Node *[num_succs];
  }
  for (unsigned int s = 0; s < num_succs; ++s) succs_[s] = NULL;
  flags_ = flags;
  player_acting_ = player_acting;
  player_folding_ = player_folding;
}

Node::~Node(void) {
  // succs_ no longer allocated out of pool
  delete [] succs_;
}

string Node::ActionName(unsigned int s) {
  if (s == CallSuccIndex()) {
    return "c";
  } else if (s == FoldSuccIndex()) {
    return "f";
  } else {
    Node *b = IthSucc(s);
    if (! b->HasCallSucc()) {
      fprintf(stderr, "Expected node to have call succ\n");
      exit(-1);
    }
    unsigned int csi = b->CallSuccIndex();
    Node *bc = b->IthSucc(csi);
    unsigned int pot_size;
    if (HasCallSucc()) {
      // I want the pot size including the current bet.
      Node *c = IthSucc(CallSuccIndex());
      pot_size = c->PotSize();
    } else {
      pot_size = PotSize();
    }
    unsigned int bet_size = (bc->PotSize() - pot_size) / 2;
    char buf[100];
    sprintf(buf, "b%u", bet_size);
    return buf;
  }
}

static void Indent(unsigned int num) {
  for (unsigned int i = 0; i < num; ++i) printf(" ");
}

void Node::PrintTree(unsigned int depth, string name,
		     unsigned int last_street) {
  Indent(2 * depth);
  unsigned int street = Street();
  if (street > last_street) name += " ";
  printf("\"%s\" (id %u ps %u ns %u s %u", name.c_str(), id_, PotSize(),
	 NumSuccs(), street);
  if (NumSuccs() > 0) {
    printf(" p%uc", player_acting_);
  }
  printf(")\n");
  unsigned int num_succs = NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    char c;
    if (s == CallSuccIndex())      c = 'C';
    else if (s == FoldSuccIndex()) c = 'F';
    else                           c = 'B';
    string new_name = name + c;
    succs_[s]->PrintTree(depth + 1, new_name, street);
  }
}

// Note that we are dependent on the ordering of the succs
unsigned int Node::CallSuccIndex(void) const {
  if (HasCallSucc()) return 0;
  else               return kMaxUInt;
}

unsigned int Node::FoldSuccIndex(void) const {
  if (HasFoldSucc()) {
    // Normally if you have a fold succ you must have a call succ too, but
    // I've done experiments where I disallow an open-call.
    if (HasCallSucc()) return 1;
    else               return 0;
  } else {
    return kMaxUInt;
  }
}

// Typically this will be the call succ.  In the unusual case where there is
// no call succ, we will use the first succ, whatever it is.  In trees
// where open-limping is prohibited, the fold succ will be the default succ.
// Note that we are dependent on the ordering of the succs
unsigned int Node::DefaultSuccIndex(void) const {
  return 0;
}

void BettingTree::Display(void) {
  root_->PrintTree(0, "", initial_street_);
}

void BettingTree::FillTerminalArray(Node *node) {
  if (node->Terminal()) {
    unsigned int terminal_id = node->TerminalID();
    if (terminal_id >= num_terminals_) {
      fprintf(stderr, "Out of bounds terminal ID: %i (num terminals %i)\n",
	      terminal_id, num_terminals_);
      exit(-1);
    }
    terminals_[terminal_id] = node;
    return;
  }
  for (unsigned int i = 0; i < node->NumSuccs(); ++i) {
    FillTerminalArray(node->IthSucc(i));
  }
}

void BettingTree::FillTerminalArray(void) {
  terminals_ = new Node *[num_terminals_];
  if (root_) FillTerminalArray(root_);
}

bool BettingTree::GetPathToNamedNode(const char *str, Node *node,
				     vector<Node *> *path) {
  char c = *str;
  // Allow an unconsumed space at the end of the name.  So we can find a node
  // either by the strictly proper name "CC " or by "CC".
  if (c == 0 || c == '\n' || (c == ' ' && (str[1] == 0 || str[1] == '\n'))) {
    return true;
  }
  if (c == ' ') return GetPathToNamedNode(str + 1, node, path);
  Node *succ;
  const char *next_str;
  if (c == 'F') {
    unsigned int s = node->FoldSuccIndex();
    succ = node->IthSucc(s);
    next_str = str + 1;
  } else if (c == 'C') {
    unsigned int s = node->CallSuccIndex();
    succ = node->IthSucc(s);
    next_str = str + 1;
  } else if (c == 'B') {
    int i = 1;
    while (str[i] >= '0' && str[i] <= '9') ++i;
    if (i == 1) {
      // Must be limit tree
      succ = node->IthSucc(node->NumSuccs() - 1);
    } else {
      char buf[20];
      if (i > 10) {
	fprintf(stderr, "Too big a bet size: %s\n", str);
	exit(-1);
      }
      // B43 - i will be 3
      memcpy(buf, str + 1, i - 1);
      buf[i-1] = 0;
      int bet_size;
      if (sscanf(buf, "%i", &bet_size) != 1) {
	fprintf(stderr, "Couldn't parse bet size: %s\n", str);
	exit(-1);
      }
      unsigned int s = node->CallSuccIndex();
      if (s == kMaxUInt) {
	// This doesn't work for graft trees
	fprintf(stderr, "GetPathToNamedNode: bet node has no call succ\n");
	exit(-1);
      }
      Node *before_call_succ = node->IthSucc(s);
      int before_pot_size = before_call_succ->PotSize();
      unsigned int num_succs = node->NumSuccs();
      unsigned int j;
      for (j = 0; j < num_succs; ++j) {
	Node *jth_succ = node->IthSucc(j);
	unsigned int s2 = jth_succ->CallSuccIndex();
	if (s2 == kMaxUInt) continue;
	Node *call_succ = jth_succ->IthSucc(s2);
	int after_pot_size = call_succ->PotSize();
	int this_bet_size = (after_pot_size - before_pot_size) / 2;
	if (this_bet_size == bet_size) break;
      }
      if (j == num_succs) {
	fprintf(stderr, "Couldn't find node with bet size %i\n", bet_size);
	exit(-1);
      }
      succ = node->IthSucc(j);
    }
    next_str = str + i;
  } else {
    fprintf(stderr, "Couldn't parse node name from %s\n", str);
    exit(-1);
  }
  if (succ == NULL) {
    return false;
  }
  path->push_back(succ);
  return GetPathToNamedNode(next_str, succ, path);
}

bool BettingTree::GetPathToNamedNode(const char *str, vector<Node *> *path) {
  path->push_back(root_);
  return GetPathToNamedNode(str, root_, path);
}

// Works for no-limit now?
// Takes a string like "BC CB" or "B100C B50" and returns the node named by
// that string
Node *BettingTree::GetNodeFromName(const char *str, Node *node) {
  char c = *str;
  // Allow an unconsumed space at the end of the name.  So we can find a node
  // either by the strictly proper name "CC " or by "CC".
  if (c == 0 || c == '\n' || (c == ' ' && (str[1] == 0 || str[1] == '\n'))) {
    return node;
  }
  if (c == ' ') return GetNodeFromName(str + 1, node);
  Node *succ;
  const char *next_str;
  if (c == 'F') {
    unsigned int s = node->FoldSuccIndex();
    succ = node->IthSucc(s);
    next_str = str + 1;
  } else if (c == 'C') {
    unsigned int s = node->CallSuccIndex();
    succ = node->IthSucc(s);
    next_str = str + 1;
  } else if (c == 'B') {
    int i = 1;
    while (str[i] >= '0' && str[i] <= '9') ++i;
    if (i == 1) {
      // Must be limit tree
      succ = node->IthSucc(node->NumSuccs() - 1);
    } else {
      char buf[20];
      if (i > 10) {
	fprintf(stderr, "Too big a bet size: %s\n", str);
	exit(-1);
      }
      // B43 - i will be 3
      memcpy(buf, str + 1, i - 1);
      buf[i-1] = 0;
      int bet_size;
      if (sscanf(buf, "%i", &bet_size) != 1) {
	fprintf(stderr, "Couldn't parse bet size: %s\n", str);
	exit(-1);
      }
      unsigned int s = node->CallSuccIndex();
      Node *before_call_succ = node->IthSucc(s);
      int before_pot_size = before_call_succ->PotSize();
      unsigned int num_succs = node->NumSuccs();
      unsigned int j;
      for (j = 0; j < num_succs; ++j) {
	Node *jth_succ = node->IthSucc(j);
	unsigned int s2 = jth_succ->CallSuccIndex();
	if (s2 == kMaxUInt) continue;
	Node *call_succ = jth_succ->IthSucc(s2);
	int after_pot_size = call_succ->PotSize();
	int this_bet_size = (after_pot_size - before_pot_size) / 2;
	if (this_bet_size == bet_size) break;
      }
      if (j == num_succs) {
	fprintf(stderr, "Couldn't find node with bet size %i\n", bet_size);
	exit(-1);
      }
      succ = node->IthSucc(j);
    }
    next_str = str + i;
  } else {
    fprintf(stderr, "Couldn't parse node name from %s\n", str);
    exit(-1);
  }
  if (succ == NULL) {
    return NULL;
  }
  return GetNodeFromName(next_str, succ);
}

Node *BettingTree::GetNodeFromName(const char *str) {
  Node *node = GetNodeFromName(str, root_);
  if (node == NULL) {
    fprintf(stderr, "Couldn't find node with name \"%s\"\n", str);
  }
  return node;
}

// Used by the subtree constructor
Node *BettingTree::Clone(Node *old_n, unsigned int *num_terminals) {
  Node *new_n = new Node(old_n);
  if (new_n->Terminal()) {
    // Need to reindex the terminal nodes
    new_n->SetTerminalID(*num_terminals);
    ++*num_terminals;
  }
  unsigned int num_succs = old_n->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Node *new_succ = Clone(old_n->IthSucc(s), num_terminals);
    new_n->SetIthSucc(s, new_succ);
  }
  return new_n;
}

Node *BettingTree::Read(Reader *reader) {
  unsigned int id = reader->ReadUnsignedIntOrDie();
  unsigned short pot_size = reader->ReadUnsignedShortOrDie();
  unsigned short num_succs = reader->ReadUnsignedShortOrDie();
  unsigned short flags = reader->ReadUnsignedShortOrDie();
  unsigned char player_acting = reader->ReadUnsignedCharOrDie();
  unsigned char player_folding = reader->ReadUnsignedCharOrDie();
  Node *node = new Node(id, pot_size, num_succs, flags, player_acting,
			player_folding);
  if (num_succs == 0) {
    ++num_terminals_;
    return node;
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    Node *succ = Read(reader);
    node->SetIthSucc(s, succ);
  }
  return node;
}

void BettingTree::Initialize(unsigned int target_player,
			     const BettingAbstraction &ba) {
  char buf[500];
  if (ba.Asymmetric()) {
    sprintf(buf, "%s/betting_tree.%s.%s.%u", Files::StaticBase(),
	    Game::GameName().c_str(),
	    ba.BettingAbstractionName().c_str(), target_player);
  } else {
    sprintf(buf, "%s/betting_tree.%s.%s", Files::StaticBase(),
	    Game::GameName().c_str(),
	    ba.BettingAbstractionName().c_str());
  }
  Reader reader(buf);
  initial_street_ = ba.InitialStreet();
  // pool_ = new Pool();
  root_ = NULL;
  terminals_ = NULL;
  num_terminals_ = 0;
  root_ = Read(&reader);
  FillTerminalArray();
  AssignNonterminalIDs(this, &num_nonterminals_);
}

BettingTree::BettingTree(void) {
}

BettingTree *BettingTree::BuildTree(const BettingAbstraction &ba) {
  BettingTree *tree = new BettingTree();
  tree->Initialize(true, ba);
  return tree;
}

BettingTree *BettingTree::BuildAsymmetricTree(const BettingAbstraction &ba,
					      unsigned int target_player) {
  BettingTree *tree = new BettingTree();
  tree->Initialize(target_player, ba);
  return tree;
}

// A subtree constructor
BettingTree *BettingTree::BuildSubtree(Node *subtree_root) {
  BettingTree *tree = new BettingTree();
  // tree->pool_ = new Pool();
  unsigned int subtree_street = subtree_root->Street();
  tree->initial_street_ = subtree_street;
  tree->num_terminals_ = 0;
  tree->root_ = tree->Clone(subtree_root, &tree->num_terminals_);
  tree->FillTerminalArray();
  AssignNonterminalIDs(tree, &tree->num_nonterminals_);
  return tree;
}

#if 0
// Don't need this any more
BettingTree *BettingTree::BuildCFRDSubtree(Node *subtree_root,
					   unsigned int root_player_acting) {
  BettingTree *tree = new BettingTree();
  // tree->pool_ = new Pool();
  unsigned int subtree_street = subtree_root->Street();
  tree->initial_street_ = subtree_street;
  tree->num_terminals_ = 0;
  Node *main_root = tree->Clone(subtree_root, &tree->num_terminals_);
  Node *t_node = new Node(tree->num_terminals_++, subtree_street, 255,
			  nullptr, nullptr, nullptr, 255,
			  subtree_root->PotSize());
  t_node->SetSpecial();
  // Treat the t_node as a bet succ of the subtree root?
  // Give it a nonterminal ID of kMaxUInt - 1.  Assume we will renumber.
  vector<Node *> bet_succs;
  bet_succs.push_back(t_node);
  tree->root_ = new Node(0, subtree_street, root_player_acting, main_root,
			 nullptr, &bet_succs, 255, subtree_root->PotSize());
  tree->FillTerminalArray();
  AssignNonterminalIDs(tree, &tree->num_nonterminals_);
  return tree;
}
#endif

void BettingTree::Delete(Node *node) {
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Delete(node->IthSucc(s));
    delete node->IthSucc(s);
  }
}

BettingTree::~BettingTree(void) {
  Delete(root_);
  delete root_;  
  // delete pool_;
  delete [] terminals_;
}

void BettingTree::GetStreetInitialNodes(Node *node, unsigned int street,
					vector<Node *> *nodes) {
  if (node->Street() == street) {
    nodes->push_back(node);
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    GetStreetInitialNodes(node->IthSucc(s), street, nodes);
  }
}

void BettingTree::GetStreetInitialNodes(unsigned int street,
					vector<Node *> *nodes) {
  nodes->clear();
  if (root_) {
    GetStreetInitialNodes(root_, street, nodes);
  }
}

// Two succs correspond if they are both call succs
// Two succs correspond if they are both fold succs
// Two succs correspond if they are both bet succs and the bet size is the
// same.
// Problem: in graft trees bet succs may not have a call succ.  So how do we
// compare if two bet succs are the same?
bool TwoSuccsCorrespond(Node *node1, unsigned int s1, Node *node2,
			unsigned int s2) {
  bool is_call_succ1 = (s1 == (unsigned int)node1->CallSuccIndex());
  bool is_call_succ2 = (s2 == (unsigned int)node2->CallSuccIndex());
  if (is_call_succ1 && is_call_succ2) return true;
  if (is_call_succ1 || is_call_succ2) return false;
  bool is_fold_succ1 = (s1 == (unsigned int)node1->FoldSuccIndex());
  bool is_fold_succ2 = (s2 == (unsigned int)node2->FoldSuccIndex());
  if (is_fold_succ1 && is_fold_succ2) return true;
  if (is_fold_succ1 || is_fold_succ2) return false;
  Node *b1 = node1->IthSucc(s1);
  Node *bc1 = b1->IthSucc(b1->CallSuccIndex());
  Node *b2 = node2->IthSucc(s2);
  Node *bc2 = b2->IthSucc(b2->CallSuccIndex());
  return (bc1->PotSize() == bc2->PotSize());
}
