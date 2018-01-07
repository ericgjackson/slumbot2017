#ifndef _BETTING_TREE_H_
#define _BETTING_TREE_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "constants.h"

using namespace std;

class BettingAbstraction;
class Reader;
class Writer;

class Node {
public:
  Node(unsigned int id, unsigned int street, unsigned int player_acting,
       const shared_ptr<Node> &call_succ, const shared_ptr<Node> &fold_succ,
       vector< shared_ptr<Node> > *bet_succs, unsigned int num_remaining,
       unsigned int bet_to);
  Node(Node *node);
  Node(unsigned int id, unsigned int last_bet_to, unsigned int num_succs,
       unsigned short flags, unsigned char player_acting,
       unsigned char num_remaining);
  ~Node(void);

  unsigned int PlayerActing(void) const {return player_acting_;}
  bool Terminal(void) const {return NumSuccs() == 0;}
  unsigned int TerminalID(void) const {return Terminal() ? id_ : kMaxUInt;}
  unsigned int NonterminalID(void) const {return Terminal() ? kMaxUInt : id_;}
  unsigned int Street(void) const {
    return (unsigned int)((flags_ & kStreetMask) >> kStreetShift);
  }
  unsigned int NumSuccs(void) const {return num_succs_;}
  Node *IthSucc(int i) const {return succs_[i].get();}
  unsigned int NumRemaining(void) const {return num_remaining_;}
  bool Showdown(void) const {return Terminal() && num_remaining_ > 1;}
  unsigned int LastBetTo(void) const {return last_bet_to_;}
  unsigned int CallSuccIndex(void) const;
  unsigned int FoldSuccIndex(void) const;
  unsigned int DefaultSuccIndex(void) const;
  string ActionName(unsigned int s);
  void PrintTree(unsigned int depth, string name, unsigned int last_street);
  bool HasCallSucc(void) const {return (bool)(flags_ & kHasCallSuccFlag);}
  bool HasFoldSucc(void) const {return (bool)(flags_ & kHasFoldSuccFlag);}
  int ID(void) const {return id_;}
  unsigned short Flags(void) const {return flags_;}
  void SetTerminalID(unsigned int id) {id_ = id;}
  void SetNonterminalID(unsigned int id) {id_ = id;}
  void SetNumSuccs(unsigned int n) {num_succs_ = n;}
  void SetIthSucc(unsigned int s, shared_ptr<Node> succ) {succs_[s] = succ;}
  void SetHasCallSuccFlag(void) {flags_ |= kHasCallSuccFlag;}
  void SetHasFoldSuccFlag(void) {flags_ |= kHasFoldSuccFlag;}
  void ClearHasCallSuccFlag(void) {flags_ &= ~kHasCallSuccFlag;}
  void ClearHasFoldSuccFlag(void) {flags_ &= ~kHasFoldSuccFlag;}

  // Bit 0: has-call-succ
  // Bit 1: has-fold-succ
  // Bit 2: special (not currently used)
  // Bits 3,4: street
  static const unsigned short kHasCallSuccFlag = 1;
  static const unsigned short kHasFoldSuccFlag = 2;
  static const unsigned short kSpecialFlag = 4;
  static const unsigned short kStreetMask = 24;
  static const int kStreetShift = 3;

 private:
  shared_ptr<Node> *succs_;
  unsigned int id_;
  unsigned short last_bet_to_;
  unsigned short num_succs_;
  unsigned short flags_;
  unsigned char player_acting_;
  unsigned char num_remaining_;
};

class BettingTree {
 public:
  ~BettingTree(void);
  void Display(void);
  bool PrintNode(unsigned int target_st, unsigned int target_pa,
		 unsigned int target_nt);
  Node *Root(void) const {return root_.get();}
  unsigned int NumTerminals(void) const {return num_terminals_;}
  unsigned int NumNonterminals(unsigned int p, unsigned int st) const {
    return num_nonterminals_[p][st];
  }
  unsigned int **NumNonterminals(void) const {return num_nonterminals_;}
  unsigned int InitialStreet(void) const {return initial_street_;}

  static BettingTree *BuildTree(const BettingAbstraction &ba);
  static BettingTree *BuildAsymmetricTree(const BettingAbstraction &ba,
					  unsigned int target_player);
  static BettingTree *BuildSubtree(Node *subtree_root);
  Node *FindNode(unsigned int st, unsigned int pa, unsigned int nt);

 private:
  BettingTree(void);

  shared_ptr<Node> Clone(Node *old_n, unsigned int *num_terminals);
  void Initialize(unsigned int target_player, const BettingAbstraction &ba);
  shared_ptr<Node>
    Read(Reader *reader,
	 unordered_map< unsigned int, shared_ptr<Node> > ***maps);

  shared_ptr<Node> root_;
  unsigned int initial_street_;
  unsigned int num_terminals_;
  unsigned int **num_nonterminals_;
};

bool PrintNode(Node *node, unsigned int target_st, unsigned int target_pa,
	       unsigned int target_nt, const string &action_sequence,
	       bool ***seen);
bool TwoSuccsCorrespond(Node *node1, unsigned int s1, Node *node2,
			unsigned int s2);

#endif
