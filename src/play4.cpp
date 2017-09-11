// This is a version of play for head-to-head comparison between two bots
// where one bot uses endgame solving.  It does an exhaustive and exact
// computation of the head-to-head EV so it is only suitable for games that
// are small enough.
//
// Normal "play" samples from the card and board possibilities and samples
// from each players' actions.  play4 instead computes "range vs. range
// equity"; i.e., it considers all possible hole card holdings either player
// may have at a certain game state.  It evaluates the head-to-head EV
// of an action sequence and a particular board.
//
// play4 is smart about only resolving the endgame as many times as is
// needed.
//
// Can I get rid of DealRawBoards()?  Seems like I have that functionality
// in the BoardTree class now.  Well, not quite.  Well, what about
// BuildCanonBoardCounts()?

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "canonical_cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "eg_cfr.h"
#include "endgames.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h" // HCPIndex()
#include "hand_value_tree.h"
#include "io.h"
#include "params.h"
#include "path.h"
#include "rand.h"
#include "runtime_config.h"
#include "runtime_params.h"
#include "sorting.h"

using namespace std;

class Player {
public:
  Player(BettingTree *base_betting_tree, BettingTree *merged_betting_tree,
	 const BettingAbstraction &base_ba,
	 const BettingAbstraction &merged_ba, const CFRConfig &a_cc,
	 const CFRConfig &b_cc, const RuntimeConfig &a_rc,
	 const RuntimeConfig &b_rc, unsigned int resolve_st);
  ~Player(void);
  void Go(void);
private:
  void Showdown(Node *node, bool a_is_p1, double *p1_probs, double *p2_probs);
  void Fold(Node *node, bool a_is_p1, double *p1_probs, double *p2_probs);
  void Process(const Path &a_path, const Path &b_path, unsigned int path_index,
	       bool a_is_p1, double *p1_probs, double *p2_probs);
  void PrepareBoard(unsigned int bd, Node * const &terminal);
  void EvalPath(const Path &a_path, const Path &b_path, bool a_is_p1);
  void WalkEndgame(Path *a_path, Path *b_path, unsigned int rbd, bool a_is_p1);
  void Resolve(const Path &base_path, const Path &merged_path, unsigned int bd,
	       bool p1, BettingTree *subtree);
  void WalkTrunk(Path *base_path, Path *merged_path, bool a_is_p1);
  void DealRawBoards(Card *board, unsigned int st, unsigned int *canon_bds,
		     unsigned int *multipliers);
  void BuildCanonBoardCounts(void);

  BettingTree *base_betting_tree_;
  BettingTree *merged_betting_tree_;
  const BettingAbstraction &base_ba_;
  const BettingAbstraction &merged_ba_;
  const CFRConfig &a_cc_;
  const CFRConfig &b_cc_;
  unsigned int resolve_st_;
  unique_ptr<CFRValues> a_trunk_probs_;
  unique_ptr<CFRValues> b_probs_;
  unique_ptr<CFRValues> a_resolve_probs_;
  unsigned int **canon_preds_;
  unsigned int **canon_board_counts_;
  unsigned int *boards_;
  unsigned int **hcps_;
  CanonicalCards *hands_;
  unsigned int canon_board_count_;
  EndgameSolver *endgame_solver_;
  double sum_p1_outcomes_;
  double sum_weights_;
  double sum_a_outcomes_;
};

void Player::Showdown(Node *node, bool a_is_p1, double *p1_probs,
		      double *p2_probs) {
  Card max_card = Game::MaxCard();
  Card max_card1 = max_card + 1;
  
  double *cum_card_probs = new double[52];
  double *total_card_probs = new double[52];
  for (Card c = 0; c <= max_card; ++c) {
    cum_card_probs[c] = 0;
    total_card_probs[c] = 0;
  }
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(node->Street());
  double sum_opp_probs = 0;
  for (unsigned int hcp = 0; hcp < num_hole_card_pairs; ++hcp) {
    const Card *cards = hands_->Cards(hcp);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    double p2_prob = p2_probs[enc];
    total_card_probs[hi] += p2_prob;
    total_card_probs[lo] += p2_prob;
    sum_opp_probs += p2_prob;
    if (p2_prob > 1.0) {
      fprintf(stderr, "p2_prob %f hcp %u\n", p2_prob, hcp);
      exit(-1);
    }
  }

  double p2_cum_prob = 0;
  double *win_probs = new double[num_hole_card_pairs];
  double half_pot = (node->PotSize() / 2);
  double sum_p1_vals = 0, sum_joint_probs = 0;

  unsigned int j = 0;
  while (j < num_hole_card_pairs) {
    unsigned int last_hand_val = hands_->HandValue(j);
    unsigned int begin_range = j;
    // Make three passes through the range of equally strong hands
    // First pass computes win counts for each hand and finds end of range
    // Second pass updates cumulative counters
    // Third pass computes lose counts for each hand
    while (j < num_hole_card_pairs) {
      const Card *cards = hands_->Cards(j);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int hand_val = hands_->HandValue(j);
      if (hand_val != last_hand_val) break;
      win_probs[j] = p2_cum_prob - cum_card_probs[hi] - cum_card_probs[lo];
      ++j;
    }
    // Positions begin_range...j-1 (inclusive) all have the same hand value
    for (unsigned int k = begin_range; k < j; ++k) {
      const Card *cards = hands_->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double p2_prob = p2_probs[enc];
      if (p2_prob <= 0) continue;
      cum_card_probs[hi] += p2_prob;
      cum_card_probs[lo] += p2_prob;
      p2_cum_prob += p2_prob;
    }
    for (unsigned int k = begin_range; k < j; ++k) {
      const Card *cards = hands_->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double p1_prob = p1_probs[enc];
      double better_hi_prob = total_card_probs[hi] - cum_card_probs[hi];
      double better_lo_prob = total_card_probs[lo] - cum_card_probs[lo];
      double lose_prob = (sum_opp_probs - p2_cum_prob) -
	better_hi_prob - better_lo_prob;
      sum_p1_vals += p1_prob * (win_probs[k] - lose_prob) * half_pot;
      // This is the sum of all P2 reach probabilities consistent
      // with P1 holding <hi, lo>.
      double sum_p2_probs = sum_opp_probs + p2_probs[enc] -
	total_card_probs[hi] - total_card_probs[lo];
      sum_joint_probs += p1_prob * sum_p2_probs;
    }
  }

  delete [] cum_card_probs;
  delete [] total_card_probs;
  delete [] win_probs;

  // Scale by board count
  double wtd_sum_p1_vals = sum_p1_vals * (double)canon_board_count_;
  double wtd_sum_joint_probs = sum_joint_probs * (double)canon_board_count_;
  sum_p1_outcomes_ += wtd_sum_p1_vals;
  sum_weights_ += wtd_sum_joint_probs;
  if (a_is_p1) sum_a_outcomes_ += wtd_sum_p1_vals;
  else         sum_a_outcomes_ -= wtd_sum_p1_vals;
#if 0
  fprintf(stderr, "Showdown %i norm P1 %f wt %f spv %f sjp %f\n",
	  node->TerminalID(), wtd_sum_p1_vals / wtd_sum_joint_probs,
	  wtd_sum_joint_probs, sum_p1_vals, sum_joint_probs);
#endif
}

void Player::Fold(Node *node, bool a_is_p1, double *p1_probs,
		  double *p2_probs) {
  Card max_card = Game::MaxCard();
  Card max_card1 = max_card + 1;

  double half_pot = (node->PotSize() / 2);
  // Give the right sign to half_pot
  if (node->P1Fold()) half_pot = -half_pot;
  double sum_p1_vals = 0, sum_joint_probs = 0;

  double *cum_card_probs = new double[52];
  double *total_card_probs = new double[52];
  for (Card c = 0; c <= max_card; ++c) {
    cum_card_probs[c] = 0;
    total_card_probs[c] = 0;
  }
  double sum_opp_probs = 0;
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(node->Street());
  for (unsigned int hcp = 0; hcp < num_hole_card_pairs; ++hcp) {
    const Card *cards = hands_->Cards(hcp);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    double p2_prob = p2_probs[enc];
    total_card_probs[hi] += p2_prob;
    total_card_probs[lo] += p2_prob;
    sum_opp_probs += p2_prob;
  }

  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands_->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    double p1_prob = p1_probs[enc];
    // This is the sum of all P2 reach probabilities consistent
    // with P1 holding <hi, lo>.
    double sum_p2_probs = sum_opp_probs + p2_probs[enc] -
      total_card_probs[hi] - total_card_probs[lo];
    sum_p1_vals += p1_prob * half_pot * sum_p2_probs;
    sum_joint_probs += p1_prob * sum_p2_probs;    
  }

  delete [] cum_card_probs;
  delete [] total_card_probs;

  // Scale by board count
  // Scale by board count
  double wtd_sum_p1_vals = sum_p1_vals * (double)canon_board_count_;
  double wtd_sum_joint_probs = sum_joint_probs * (double)canon_board_count_;
  sum_p1_outcomes_ += wtd_sum_p1_vals;
  sum_weights_ += wtd_sum_joint_probs;
  if (a_is_p1) sum_a_outcomes_ += wtd_sum_p1_vals;
  else         sum_a_outcomes_ -= wtd_sum_p1_vals;
#if 0
  fprintf(stderr, "Fold %i A is %s norm P1 %f wt %f\n", node->TerminalID(),
	  a_is_p1 ? "P1" : "P2", wtd_sum_p1_vals / wtd_sum_joint_probs,
	  wtd_sum_joint_probs);
#endif
}

void Player::Process(const Path &a_path, const Path &b_path,
		     unsigned int path_index, bool a_is_p1, double *p1_probs,
		     double *p2_probs) {
  Node *a_node = a_path.IthNode(path_index);
  Node *b_node = b_path.IthNode(path_index);
  if (path_index == a_path.Size() - 1) {
    if (a_node->Showdown()) {
      Showdown(a_node, a_is_p1, p1_probs, p2_probs);
    } else {
      Fold(a_node, a_is_p1, p1_probs, p2_probs);
    }
    return;
  }
  bool p1 = a_node->P1Choice();
  unsigned int st = a_node->Street();
  bool a_is_acting = a_is_p1 == p1;
  const CFRValues &probs = a_is_acting ?
    (st >= resolve_st_ ? *a_resolve_probs_ : *a_trunk_probs_) : *b_probs_;

  unsigned int s = a_path.IthSucc(path_index);
  unsigned int num_succs = a_node->NumSuccs();
  unsigned int nt;
  if (a_is_acting) nt = a_node->NonterminalID();
  else             nt = b_node->NonterminalID();
  unsigned int bd = boards_[st];
  if (a_is_acting && st >= resolve_st_) {
    // We are within a subtree.  Change bd to the local board index.
    bd = BoardTree::LocalIndex(resolve_st_, boards_[resolve_st_], st, bd);
  }
  unsigned int dsi = a_node->DefaultSuccIndex();
  Card max_card1 = Game::MaxCard() + 1;
  unsigned int tst = a_path.Last()->Street();
  unsigned int num_st_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int num_tst_hole_card_pairs = Game::NumHoleCardPairs(tst);
  // Note: we are iterating over all hole card pairs for the terminal street
  // which may be a later street than the current street.
  for (unsigned int tst_hcp = 0; tst_hcp < num_tst_hole_card_pairs; ++tst_hcp) {
    const Card *cards = hands_->Cards(tst_hcp);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    unsigned int hcp;
    if (st == tst) hcp = tst_hcp;
    else           hcp = hcps_[tst_hcp][st];
    double prob = probs.Prob(p1, st, nt, bd, hcp, num_st_hole_card_pairs,
			     s, num_succs, dsi);
    if (p1) p1_probs[enc] *= prob;
    else    p2_probs[enc] *= prob;
    if (prob > 1.0) {
      fprintf(stderr, "prob %f enc %u st %u nt %u bd %u hcp %u s %u aisp1 %i "
	      "p1 %i A\n", prob, enc, st, nt, bd, hcp, s, (int)a_is_p1,
	      (int)p1);
      exit(-1);
    }
  }
  Process(a_path, b_path, path_index + 1, a_is_p1, p1_probs, p2_probs);
}

// It's ugly that we create hands_ inside this function and then need to
// remember to delete it outside.
void Player::PrepareBoard(unsigned int bd, Node * const &terminal) {
  unsigned int tst = terminal->Street();
  canon_board_count_ = canon_board_counts_[tst][bd];
  const Card *board = BoardTree::Board(tst, bd);

  boards_[tst] = bd;
  for (unsigned int pst = 1; pst < tst; ++pst) {
    boards_[pst] = canon_preds_[tst][bd * tst + pst];
  }
  
  unsigned int num_board_cards = Game::NumBoardCards(tst);
  unsigned int sg = BoardTree::SuitGroups(tst, bd);
  hands_ = new CanonicalCards(2, board, num_board_cards, sg, false);
  if (tst == Game::MaxStreet()) {
    hands_->SortByHandStrength(board);
  }

  Card hand_cards[7];
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    hand_cards[num_hole_cards + i] = board[i];
  }

  unsigned int num_tst_hole_card_pairs = Game::NumHoleCardPairs(tst);
  for (unsigned int tst_hcp = 0; tst_hcp < num_tst_hole_card_pairs;
       ++tst_hcp) { 
    const Card *cards = hands_->Cards(tst_hcp);
    Card hi = cards[0];
    Card lo = cards[1];
    hand_cards[0] = hi;
    hand_cards[1] = lo;

    // Confusing.  We will store buckets under the index into hands_.
    // But we need to look up the bucket for street st under the hcp index of
    // st.
    //
    // I only hole card pair indices for streets prior to the terminal node.
    for (unsigned int pst = 0; pst < tst; ++pst) {
      hcps_[tst_hcp][pst] = HCPIndex(pst, hand_cards);
    }
  }
}

// Betting sequence is given.
void Player::EvalPath(const Path &a_path, const Path &b_path, bool a_is_p1) {
  Card max_card1 = Game::MaxCard() + 1;
  unsigned int num = max_card1 * max_card1;
  double *p1_probs = new double[num];
  double *p2_probs = new double[num];
  unsigned int tst = a_path.Last()->Street();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(tst);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands_->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    p1_probs[enc] = 1.0;
    p2_probs[enc] = 1.0;
  }

  Process(a_path, b_path, 0, a_is_p1, p1_probs, p2_probs);

  delete [] p1_probs;
  delete [] p2_probs;
}

void Player::WalkEndgame(Path *a_path, Path *b_path, unsigned int rbd,
			 bool a_is_p1) {
  Node *a_node = a_path->Last();
  Node *b_node = b_path->Last();
  if (a_node->Terminal()) {
    unsigned int st = a_node->Street();
    if (st > resolve_st_) {
      unsigned int num_local_boards =
	BoardTree::NumLocalBoards(resolve_st_, rbd, st);
      for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
	unsigned int gbd = BoardTree::GlobalIndex(resolve_st_, rbd, st, lbd);
	PrepareBoard(gbd, a_node);
	EvalPath(*a_path, *b_path, a_is_p1);
	delete hands_;
	hands_ = NULL;
      }
    } else {
      PrepareBoard(rbd, a_node);
      EvalPath(*a_path, *b_path, a_is_p1);
      delete hands_;
      hands_ = NULL;
    }
    return;
  }
  unsigned int num_succs = a_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Node *a_succ = a_node->IthSucc(s);
    Node *b_succ = b_node->IthSucc(s);
    a_path->SetLastSucc(s);
    a_path->Push(a_succ);
    b_path->SetLastSucc(s);
    b_path->Push(b_succ);
    WalkEndgame(a_path, b_path, rbd, a_is_p1);
    a_path->Pop();
    b_path->Pop();
  }
}

// Does a_probs_ hold probabilities for the whole tree or just for the
// current subgame?  If for the whole tree, I think I need a Read() method
// to read in just a subgame.  Current Read() method only supports reading
// all of the values.
// Easier maybe to have a_trunk_probs_ and a_subgame_probs_.  Need to
// construct a_subgame_probs_ here.
void Player::Resolve(const Path &base_path, const Path &merged_path,
		     unsigned int bd, bool p1, BettingTree *subtree) {
  Node *base_subtree_root = base_path.Last();
  Node *merged_subtree_root = merged_path.Last();
  unsigned int base_subtree_nt = base_subtree_root->NonterminalID();
  unsigned int max_street = Game::MaxStreet();
  // Number of iterations should be a parameter
  unsigned int num_its = 200;
  endgame_solver_->Solve(merged_path.Nodes(), bd, base_subtree_nt, num_its,
			 p1, ! p1);
  char dir[500];
  sprintf(dir, "%s/%s.null.%u.%u.%u.%s.%s/endgames.%s.%u",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_ba_.BettingAbstractionName().c_str(),
	  a_cc_.CFRConfigName().c_str(),
	  merged_ba_.BettingAbstractionName().c_str(), resolve_st_);
  bool *streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    streets[st] = st >= resolve_st_;
  }
  a_resolve_probs_.reset(new CFRValues(p1, ! p1, true, streets,
				       subtree->NumNonterminals(), bd,
				       resolve_st_, kMaxUInt));
  delete [] streets;
  a_resolve_probs_->Read(dir, num_its, subtree->Root(),
			 merged_subtree_root->NonterminalID(),
			 p1 ? 1 : 0);
}

void Player::WalkTrunk(Path *base_path, Path *merged_path, bool a_is_p1) {
  Node *base_node = base_path->Last();
  Node *merged_node = merged_path->Last();
  if (base_node->Terminal()) {
    unsigned int st = base_node->Street();
    unsigned int num_boards = BoardTree::NumBoards(st);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      PrepareBoard(bd, merged_node);
      EvalPath(*merged_path, *merged_path, a_is_p1);
      delete hands_;
      hands_ = NULL;
    }
    return;
  }
  if (base_node->Street() == resolve_st_) {
    unsigned int num_boards = BoardTree::NumBoards(resolve_st_);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      Node *merged_subtree_root = merged_path->Last();
      BettingTree *subtree = BettingTree::BuildSubtree(merged_subtree_root);
      Resolve(*base_path, *merged_path, bd, a_is_p1, subtree);
      Path *a_path = new Path(*merged_path);
      a_path->SetLastNode(subtree->Root());
      WalkEndgame(a_path, merged_path, bd, a_is_p1);
      delete a_path;
      delete subtree;
      a_resolve_probs_.reset(nullptr);
    }
    return;
  }
  unsigned int num_succs = base_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Node *base_succ = base_node->IthSucc(s);
    Node *merged_succ = merged_node->IthSucc(s);
    base_path->SetLastSucc(s);
    base_path->Push(base_succ);
    merged_path->SetLastSucc(s);
    merged_path->Push(merged_succ);
    WalkTrunk(base_path, merged_path, a_is_p1);
    base_path->Pop();
    merged_path->Pop();
  }
}

void Player::Go(void) {
  sum_p1_outcomes_ = 0;
  sum_weights_ = 0;
  Path base_path, merged_path;
  base_path.Push(base_betting_tree_->Root());
  merged_path.Push(merged_betting_tree_->Root());
  for (unsigned int a_is_p1 = 0; a_is_p1 <= 1; ++a_is_p1) {
    WalkTrunk(&base_path, &merged_path, a_is_p1);
  }
  double avg_a_outcome = sum_a_outcomes_ / sum_weights_;
  // avg_a_outcome is in units of the small blind
  double a_mbb_g = (avg_a_outcome / 2.0) * 1000.0;
  printf("Avg A outcome: %f (%.1f mbb/g)\n", avg_a_outcome, a_mbb_g);
  double avg_p1_outcome = sum_p1_outcomes_ / sum_weights_;
  // avg_p1_outcome is in units of the small blind
  double p1_mbb_g = (avg_p1_outcome / 2.0) * 1000.0;
  printf("Avg P1 outcome: %f (%.1f mbb/g) sum_weights %f\n", avg_p1_outcome,
	 p1_mbb_g, sum_weights_);
  fflush(stdout);
}

void Player::DealRawBoards(Card *board, unsigned int st,
			   unsigned int *canon_bds, unsigned int *multipliers) {
  unsigned int max_street = Game::MaxStreet();
  if (st > 1) {
    unsigned int num_board_cards = Game::NumBoardCards(st - 1);
    Card canon_board[5];
    bool change_made = 
      CanonicalCards::ToCanon2(board, num_board_cards, 0, canon_board);
    // I need this, right?  ToCanon2() can change the suits of the cards,
    // which could make, e.g., the flop no longer be ordered from high to low.
    if (change_made) {
      num_board_cards = 0;
      for (unsigned int st1 = 1; st1 <= st - 1; ++st1) {
	unsigned int num_street_cards = Game::NumCardsForStreet(st1);
	SortCards(canon_board + num_board_cards, num_street_cards);
	num_board_cards += num_street_cards;
      }
    }
    unsigned int canon_bd = BoardTree::LookupBoard(canon_board, st - 1);
    canon_bds[st - 1] = canon_bd;
    canon_board_counts_[st - 1][canon_bd] += multipliers[st-1];
    unsigned int *my_preds = &canon_preds_[st - 1][canon_bd * (st - 1)];
    for (unsigned int st1 = 0; st1 < st - 1; ++st1) {
      my_preds[st1] = canon_bds[st1];
    }
    if (st == max_street + 1) {
      return;
    }
  }
  unsigned int num_street_cards = Game::NumCardsForStreet(st);
  unsigned int num_prev_board_cards = Game::NumBoardCards(st - 1);
  Card max_card = Game::MaxCard();
  if (num_street_cards == 1) {
    for (Card c = 0; c <= max_card; ++c) {
      if (InCards(c, board, num_prev_board_cards)) continue;
      board[num_prev_board_cards] = c;
      DealRawBoards(board, st + 1, canon_bds, multipliers);
    }
  } else if (num_street_cards == 2) {
    for (Card hi = 1; hi <= max_card; ++hi) {
      if (InCards(hi, board, num_prev_board_cards)) continue;
      board[num_prev_board_cards] = hi;
      for (Card lo = 0; lo < hi; ++lo) {
	if (InCards(lo, board, num_prev_board_cards)) continue;
	board[num_prev_board_cards + 1] = lo;
	DealRawBoards(board, st + 1, canon_bds, multipliers);
      }
    }
  } else if (num_street_cards == 3) {
    for (Card hi = 2; hi <= max_card; ++hi) {
      if (InCards(hi, board, num_prev_board_cards)) continue;
      board[num_prev_board_cards] = hi;
      for (Card mid = 1; mid < hi; ++mid) {
	if (InCards(mid, board, num_prev_board_cards)) continue;
	board[num_prev_board_cards + 1] = mid;
	for (Card lo = 0; lo < mid; ++lo) {
	  if (InCards(lo, board, num_prev_board_cards)) continue;
	  board[num_prev_board_cards + 2] = lo;
	  DealRawBoards(board, st + 1, canon_bds, multipliers);
	}
      }
    }
  } else {
    fprintf(stderr, "Can't handle %u street cards\n", num_street_cards);
    exit(-1);
  }
}

static unsigned int Factorial(unsigned int n) {
  if (n == 0) return 1;
  if (n == 1) return 1;
  return n * Factorial(n - 1);
}

// Wait, I might need to scale up the non-max-street board counts by the
// number of board completions.
void Player::BuildCanonBoardCounts(void) {
  unsigned int max_street = Game::MaxStreet();
  canon_board_counts_ = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int num_boards = BoardTree::NumBoards(st);
    canon_board_counts_[st] = new unsigned int[num_boards];
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      canon_board_counts_[st][bd] = 0;
    }
  }
  
  canon_preds_ = new unsigned int *[max_street + 1];
  canon_preds_[0] = NULL;
  for (unsigned int st = 1; st <= max_street; ++st) {
    unsigned int num_boards = BoardTree::NumBoards(st);
    canon_preds_[st] = new unsigned int[num_boards * st];
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      for (unsigned int st1 = 0; st1 < st; ++st1) {
	canon_preds_[st][bd * st + st1] = kMaxUInt;
      }
    }
  }

  unsigned int *multipliers = new unsigned int[max_street + 1];
  multipliers[max_street] = 1;
  for (int st = ((int)max_street) - 1; st >= 0; --st) {
    multipliers[st] = multipliers[st+1] * Game::StreetPermutations(st+1);
  }
  
  unsigned int *canon_bds = new unsigned int[max_street + 1];
  canon_bds[0] = 0;
  Card board[5];
  DealRawBoards(board, 1, canon_bds, multipliers);
  canon_board_counts_[0][0] = multipliers[0];

  delete [] multipliers;
}

Player::Player(BettingTree *base_betting_tree,
	       BettingTree *merged_betting_tree,
	       const BettingAbstraction &base_ba,
	       const BettingAbstraction &merged_ba,
	       const CFRConfig &a_cc, const CFRConfig &b_cc,
	       const RuntimeConfig &a_rc, const RuntimeConfig &b_rc,
	       unsigned int resolve_st) :
  base_ba_(base_ba), merged_ba_(merged_ba), a_cc_(a_cc), b_cc_(b_cc) {
  base_betting_tree_ = base_betting_tree;
  merged_betting_tree_ = merged_betting_tree;
  resolve_st_ = resolve_st;
  BoardTree::CreateLookup();
  BuildCanonBoardCounts();
  BoardTree::DeleteLookup();
  unsigned int max_street = Game::MaxStreet();

  bool *trunk_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    trunk_streets[st] = st < resolve_st_;
  }
  a_trunk_probs_.reset(new CFRValues(true, true, true, trunk_streets,
				     merged_betting_tree_->NumNonterminals(),
				     0, 0, kMaxUInt));
  delete [] trunk_streets;

  unsigned int a_it = a_rc.Iteration();
  char dir[500];
  sprintf(dir, "%s/%s.null.%u.%u.%u.%s.%s",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_ba_.BettingAbstractionName().c_str(),
	  a_cc_.CFRConfigName().c_str());
  a_trunk_probs_->Read(dir, a_it, base_betting_tree_->Root(), 0, kMaxUInt);
  
  b_probs_.reset(new CFRValues(true, true, true, nullptr,
			       merged_betting_tree_->NumNonterminals(),
			       0, 0, kMaxUInt));
  unsigned int b_it = b_rc.Iteration();
  sprintf(dir, "%s/%s.null.%u.%u.%u.%s.%s",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_ba_.BettingAbstractionName().c_str(),
	  b_cc_.CFRConfigName().c_str());
  b_probs_->Read(dir, b_it, merged_betting_tree->Root(), 0, kMaxUInt);

  boards_ = new unsigned int[max_street + 1];
  boards_[0] = 0;
  
  unsigned int max_num_hole_card_pairs = Game::NumHoleCardPairs(0);
  hcps_ = new unsigned int *[max_num_hole_card_pairs];
  for (unsigned int hcp = 0; hcp < max_num_hole_card_pairs; ++hcp) {
    hcps_[hcp] = new unsigned int[max_street];
  }

  if (resolve_st_ <= max_street) {
    endgame_solver_ = new EndgameSolver(resolve_st_, *a_trunk_probs_,
					a_rc.Iteration(), base_ba_,
					merged_ba_, a_cc_,
					ResolvingMethod::CFRD, 1);
  } else {
    endgame_solver_ = NULL;
  }
}

Player::~Player(void) {
  delete endgame_solver_;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    delete [] canon_board_counts_[st];
    delete [] canon_preds_[st];
  }
  delete [] canon_board_counts_;
  delete [] canon_preds_;
  delete [] boards_;
  unsigned int max_num_hole_card_pairs = Game::NumHoleCardPairs(0);
  for (unsigned int hcp = 0; hcp < max_num_hole_card_pairs; ++hcp) {
    delete [] hcps_[hcp];
  }
  delete [] hcps_;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base betting abstraction params> "
	  "<merged betting abstraction params> <A CFR params> "
	  "<B CFR params> <A runtime params> <B runtime params> "
	  "<A it> <B it> <resolve st>\n",
	  prog_name);
  fprintf(stderr, "\n");
  fprintf(stderr, "Use resolve_st of -1 for no resolving (supported?)\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 11) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> base_betting_params = CreateBettingAbstractionParams();
  base_betting_params->ReadFromFile(argv[2]);
  unique_ptr<BettingAbstraction>
    base_betting_abstraction(new BettingAbstraction(*base_betting_params));
  unique_ptr<Params> merged_betting_params = CreateBettingAbstractionParams();
  merged_betting_params->ReadFromFile(argv[3]);
  unique_ptr<BettingAbstraction> merged_betting_abstraction(
		      new BettingAbstraction(*merged_betting_params));
  unique_ptr<Params> a_cfr_params = CreateCFRParams();
  a_cfr_params->ReadFromFile(argv[4]);
  unique_ptr<CFRConfig>
    a_cfr_config(new CFRConfig(*a_cfr_params));
  unique_ptr<Params> b_cfr_params = CreateCFRParams();
  b_cfr_params->ReadFromFile(argv[5]);
  unique_ptr<CFRConfig>
    b_cfr_config(new CFRConfig(*b_cfr_params));
  unique_ptr<Params> a_runtime_params = CreateRuntimeParams();
  a_runtime_params->ReadFromFile(argv[6]);
  unique_ptr<RuntimeConfig>
    a_runtime_config(new RuntimeConfig(*a_runtime_params));
  unique_ptr<Params> b_runtime_params = CreateRuntimeParams();
  b_runtime_params->ReadFromFile(argv[7]);
  unique_ptr<RuntimeConfig>
    b_runtime_config(new RuntimeConfig(*b_runtime_params));

  unsigned int a_it, b_it;
  if (sscanf(argv[8], "%u", &a_it) != 1) Usage(argv[0]);
  if (sscanf(argv[9], "%u", &b_it) != 1) Usage(argv[0]);
  int rst;
  if (sscanf(argv[10], "%i", &rst) != 1) Usage(argv[0]);
  unsigned int resolve_st;
  if (rst == -1) resolve_st = kMaxUInt;
  else           resolve_st = rst;

  a_runtime_config->SetIteration(a_it);
  b_runtime_config->SetIteration(b_it);
  fprintf(stderr, "A iteration %llu\n", a_runtime_config->Iteration());
  fprintf(stderr, "B iteration %llu\n", b_runtime_config->Iteration());

  BoardTree::Create();
  HandValueTree::Create();

  // Leave this in if we don't want reproducibility
  InitRand();
  BettingTree *base_betting_tree =
    BettingTree::BuildTree(*base_betting_abstraction);
  BettingTree *merged_betting_tree =
    BettingTree::BuildTree(*merged_betting_abstraction);

  Player *player = new Player(base_betting_tree, merged_betting_tree,
			      *base_betting_abstraction,
			      *merged_betting_abstraction, *a_cfr_config,
			      *b_cfr_config, *a_runtime_config,
			      *b_runtime_config, resolve_st);
  player->Go();
  delete player;
  delete base_betting_tree;
  delete merged_betting_tree;
}
