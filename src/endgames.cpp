#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "endgames.h"
#include "eg_cfr.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#if 0
#include "os_cfr.h"
#include "pcs_cfr.h"
#endif

using namespace std;

EndgameSolver::EndgameSolver(unsigned int solve_street,
			     unsigned int base_it,
			     const CardAbstraction &base_ca,
			     const CardAbstraction &endgame_ca,
			     const BettingAbstraction &base_ba,
			     const BettingAbstraction &endgame_ba,
			     const CFRConfig &base_cc,
			     const CFRConfig &endgame_cc,
			     const Buckets &base_buckets,
			     const Buckets &endgame_buckets,
			     BettingTree *base_betting_tree,
			     ResolvingMethod method, bool cfrs,
			     bool card_level, bool zero_sum,
			     unsigned int num_threads) :
  VCFR(base_ca, base_ba, base_cc, base_buckets, base_betting_tree,
       num_threads) {
  solve_street_ = solve_street;
  base_it_ = base_it;
  method_ = method;
  card_level_ = card_level;
  num_hole_card_pairs_ = Game::NumHoleCardPairs(solve_street_);
  BoardTree::Create();
  BoardTree::CreateLookup();
  // Used for best-response estimation
  BoardTree::BuildBoardCounts();

  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  num_nonterminals_ = new unsigned int *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    num_nonterminals_[p] = new unsigned int[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      num_nonterminals_[p][st] = betting_tree_->NumNonterminals(p, st);
    }
  }
  
  unique_ptr<bool []> streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    streets[st] = st < solve_street_;
  }
  
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported\n");
    exit(-1);
#if 0
    char buf[100];
    sprintf(buf, ".p%u", p_^1);
    strcat(dir, buf);
#endif
  }

  sumprobs_.reset(new CFRValues(nullptr, true, streets.get(),
				betting_tree_, 0, 0, card_abstraction_,
				buckets_, compressed_streets_));
  sumprobs_->Read(dir, base_it_, betting_tree_->Root(), "x", kMaxUInt);

  if (endgame_cc.Algorithm() == "cfrp") {
    eg_cfr_ = new EGCFR(endgame_ca, base_ca, endgame_ba, base_ba, endgame_cc,
			base_cc, endgame_buckets, solve_street_, method,
			cfrs, zero_sum, num_threads);
#if 0
  } else if (endgame_cc.Algorithm() == "pcs") {
    eg_cfr_ = new EGPCSCFR(endgame_ca, base_ca, endgame_ba, base_ba,
			   endgame_cc, base_cc, endgame_buckets, num_threads);
  } else if (endgame_cc.Algorithm() == "os") {
    eg_cfr_ = new EGOSCFR(endgame_ca, base_ca, endgame_ba, base_ba,
			   endgame_cc, base_cc, endgame_buckets, num_threads);
#endif
  } else {
    fprintf(stderr, "Unknown CFR algorithm: %s\n",
	    endgame_cc.Algorithm().c_str());
    exit(-1);
  }

  br_vals_ = new double ****[2];
  unsigned int num_boards = BoardTree::NumBoards(solve_street_);
  for (unsigned int pr = 0; pr <= 1; ++pr) {
    br_vals_[pr] = new double ***[2];
    for (unsigned int pa = 0; pa <= 1; ++pa) {
      unsigned int num_nt =
	betting_tree_->NumNonterminals(pa, solve_street_);
      br_vals_[pr][pa] = new double **[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	br_vals_[pr][pa][i] = new double *[num_boards];
	for (unsigned int bd = 0; bd < num_boards; ++bd) {
	  br_vals_[pr][pa][i][bd] = nullptr;
	}
      }
    }
  }
  fflush(stdout);
#if 0
  p0_br_cum_ = 0;
  p1_br_cum_ = 0;
  p0_br_sum_weights_ = 0;
  p1_br_sum_weights_ = 0;
#endif
}

EndgameSolver::~EndgameSolver(void) {
  delete eg_cfr_;
  // Need to delete br_vals_.
  unsigned int num_boards = BoardTree::NumBoards(solve_street_);
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int pr = 0; pr < num_players; ++pr) {
    for (unsigned int pa = 0; pa < num_players; ++pa) {
      unsigned int num_nt = num_nonterminals_[pa][solve_street_];
      for (unsigned int i = 0; i < num_nt; ++i) {
	for (unsigned int bd = 0; bd < num_boards; ++bd) {
	  // These should already have been deleted, but in case not
	  delete [] br_vals_[pr][pa][i][bd];
	}
	delete [] br_vals_[pr][pa][i];
      }
      fflush(stdout);
      delete [] br_vals_[pr][pa];
    }
    fflush(stdout);
    delete [] br_vals_[pr];
  }
  delete [] br_vals_;
  
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] num_nonterminals_[p];
  }
  delete [] num_nonterminals_;
}

void EndgameSolver::BRGo(double *p0_br, double *p1_br) {
  value_calculation_ = true;

  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
  bool bucketed = false;
  for (unsigned int st = 0; st < solve_street_; ++st) {
    bucketed_streets[st] = ! buckets_.None(st);
    if (bucketed_streets[st]) bucketed = true;
  }
  for (unsigned int st = solve_street_; st <= max_street; ++st) {
    bucketed_streets[st] = false;
  }
  if (bucketed) {
    // Would be more memory efficient to calculate current strategy probs
    // on the fly.
    current_strategy_.reset(new CFRValues(nullptr, false,
					  bucketed_streets.get(),
					  betting_tree_, 0, 0,
					  card_abstraction_, buckets_,
					  compressed_streets_));
    current_strategy_->AllocateAndClearDoubles(betting_tree_->Root(),
					       kMaxUInt);
  } else {
    current_strategy_.reset(nullptr);
  }
  if (current_strategy_.get() != nullptr) {
    SetCurrentStrategy(betting_tree_->Root());
  }

  unsigned int num_hole_cards = Game::NumCardsForStreet(0);

  // Unfortunate that we need to pass in solve_street_, not solve_street_-1.
  // In StreetInitial() we need the hands for the next street.
  hand_tree_ = new HandTree(0, 0, solve_street_);
  double *opp_probs = AllocateOppProbs(true);
  unsigned int **street_buckets = AllocateStreetBuckets();
  VCFRState state(opp_probs, street_buckets, hand_tree_);
  SetStreetBuckets(0, 0, state);

  p_ = 0;
  double *p0_vals = Process(betting_tree_->Root(), 0, state, 0);

  p_ = 1;
  double *p1_vals = Process(betting_tree_->Root(), 0, state, 0);

  DeleteStreetBuckets(street_buckets);
  delete [] opp_probs;
  delete hand_tree_;

  unsigned int num_remaining = Game::NumCardsInDeck() - num_hole_cards;
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_opp_hole_card_pairs =
    num_remaining * (num_remaining - 1) / 2;
  unsigned int denom = num_hole_card_pairs * num_opp_hole_card_pairs;
  double p0_sum = 0, p1_sum = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    p0_sum += p0_vals[i];
    p1_sum += p1_vals[i];
  }
  *p0_br = p0_sum / denom;
  *p1_br = p1_sum / denom;

  delete [] p0_vals;
  delete [] p1_vals;
}

double *EndgameSolver::Process(Node *node, unsigned int lbd, 
			       const VCFRState &state, unsigned int last_st) {
  unsigned int st = node->Street();
  if (st == solve_street_ && last_st == solve_street_) {
    // Want to get here after the call to StreetInitial() in VCFR which will
    // loop over the boards on solve_street_.
    unsigned int nt = node->NonterminalID();
    unsigned int pa = node->PlayerActing();
    double *vals = br_vals_[p_][pa][nt][lbd];
    br_vals_[p_][pa][nt][lbd] = nullptr;
    return vals;
  } else {
    return VCFR::Process(node, lbd, state, last_st);
  }
}

void EndgameSolver::GetReachProbs(const vector<Node *> *base_path,
				  const Card *board, unsigned int *prior_bds,
				  double **reach_probs) {
  unsigned int max_card = Game::MaxCard();
  unsigned int path_len = base_path->size();
  Card cards[7];
  unsigned int num_board_cards = Game::NumBoardCards(solve_street_);
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    cards[i + 2] = board[i];
  }
  for (unsigned int i = 0; i < path_len - 1; ++i) {
    Node *node = (*base_path)[i];
    unsigned int p = node->PlayerActing();
    unsigned int st = node->Street();
    if (st > solve_street_) {
      fprintf(stderr, "EndgameSolver::Solver: st > solve_street_?!?\n");
      exit(-1);
    }
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int nt = node->NonterminalID();
    unsigned int dsi = node->DefaultSuccIndex();
    // unsigned int fsi = node->FoldSuccIndex();
    Node *next = (*base_path)[i+1];
    unsigned int num_succs = node->NumSuccs();
    unsigned int s;
    for (s = 0; s < num_succs; ++s) {
      if (node->IthSucc(s) == next) break;
    }
    if (s == num_succs) {
      fprintf(stderr,
	      "EndgameSolver::Solver: node doesn't connect to next?!?\n");
      exit(-1);
    }
    unsigned int bd = prior_bds[st];
    for (unsigned int hi = 1; hi <= max_card; ++hi) {
      if (InCards(hi, board, num_board_cards)) continue;
      cards[0] = hi;
      for (unsigned int lo = 0; lo < hi; ++lo) {
	if (InCards(lo, board, num_board_cards)) continue;
	cards[1] = lo;
	unsigned int enc = hi * (max_card + 1) + lo;
	// We know we are not on the final street
	unsigned int hcp = HCPIndex(st, cards);
	unsigned int offset;
	if (buckets_.None(st)) {
	  offset = bd * num_hole_card_pairs * num_succs + hcp * num_succs;
	} else {
	  unsigned int h = bd * num_hole_card_pairs + hcp;
	  unsigned int b = buckets_.Bucket(st, h);
	  offset = b * num_succs;
	}
	double prob = sumprobs_->Prob(p, st, nt, offset, s, num_succs, dsi);
	if (prob > 1.0) {
	  fprintf(stderr, "Base prob > 1\n");
	  exit(-1);
	}
	reach_probs[p][enc] *= prob;
      }
    }
  }
}

// Solve a subgame rooted at the last node of the path passed in.  The
// starting distribution of hands is based on the base probs for the nodes
// along the path.
//
// subtree_root is a node in the endgame betting tree which may be different
// from the base betting tree.
void EndgameSolver::SolveSafe(Node *solve_root, Node *target_root,
			      unsigned int solve_bd, unsigned int target_bd,
			      const string &action_sequence,
			      const vector<Node *> *base_path, 
			      unsigned int num_its, bool p0, bool p1) {
  unsigned int street = solve_root->Street();
  if (street != solve_street_) {
    fprintf(stderr, "EndgameSolver::Solver: street != solve_street_?!?\n");
    exit(-1);
  }
  unsigned int max_street = Game::MaxStreet();
  unsigned int *prior_bds = new unsigned int[max_street + 1];
  prior_bds[0] = 0;
  prior_bds[solve_street_] = solve_bd;
  const Card *board = BoardTree::Board(solve_street_, solve_bd);
  for (unsigned int st = 1; st < solve_street_; ++st) {
    prior_bds[st] = BoardTree::LookupBoard(board, st);
  }
  unsigned int max_card = Game::MaxCard();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_encodings;
  if (num_hole_cards == 1) num_encodings = max_card + 1;
  else                     num_encodings = (max_card + 1) * (max_card + 1);
  double **reach_probs = new double *[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    reach_probs[p] = new double[num_encodings];
    for (unsigned int i = 0; i < num_encodings; ++i) {
      reach_probs[p][i] = 1.0;
    }
  }
  GetReachProbs(base_path, board, prior_bds, reach_probs);
  delete [] prior_bds;

  HandTree *hand_tree = new HandTree(solve_street_, solve_bd, max_street);

  BettingTree *subtree = BettingTree::BuildSubtree(solve_root);

  const CanonicalCards *hands = hand_tree->Hands(solve_street_, 0);
  if (p0) {
    double *opp_cvs = eg_cfr_->LoadOppCVs(solve_root, action_sequence,
					  solve_bd, 0, base_it_, reach_probs,
					  hands, card_level_);
    eg_cfr_->SolveSubgame(subtree, solve_bd, reach_probs, action_sequence,
			  hand_tree, opp_cvs, 0, false, num_its);
    eg_cfr_->Write(subtree, solve_root, target_root, action_sequence, num_its,
		   target_bd);
    delete [] opp_cvs;
  }
  if (p1) {
    double *opp_cvs = eg_cfr_->LoadOppCVs(solve_root, action_sequence,
					  solve_bd, 1, base_it_, reach_probs,
					  hands, card_level_);
    eg_cfr_->SolveSubgame(subtree, solve_bd, reach_probs, action_sequence,
			  hand_tree, opp_cvs, 1, false, num_its);
    eg_cfr_->Write(subtree, solve_root, target_root, action_sequence, num_its,
		   target_bd);
    delete [] opp_cvs;
  }
  delete subtree;
  delete hand_tree;
  for (unsigned int p = 0; p <= 1; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;
}

// Solve a subgame rooted at the last node of the path passed in.  The
// starting distribution of hands is based on the base probs for the nodes
// along the path.
void EndgameSolver::SolveUnsafe(Node *solve_root, Node *target_root,
				unsigned int solve_bd, unsigned int target_bd,
				unsigned int base_target_nt,
				const string &action_sequence,
				const vector<Node *> *base_path,
				unsigned int num_its) {
  unsigned int street = solve_root->Street();
  if (street != solve_street_) {
    fprintf(stderr, "EndgameSolver::Solver: street != solve_street_?!?\n");
    exit(-1);
  }
  unsigned int max_street = Game::MaxStreet();
  unsigned int *prior_bds = new unsigned int[max_street + 1];
  prior_bds[0] = 0;
  prior_bds[solve_street_] = solve_bd;
  const Card *board = BoardTree::Board(solve_street_, solve_bd);
  for (unsigned int st = 1; st < solve_street_; ++st) {
    prior_bds[st] = BoardTree::LookupBoard(board, st);
  }
  unsigned int max_card = Game::MaxCard();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_encodings;
  if (num_hole_cards == 1) num_encodings = max_card + 1;
  else                     num_encodings = (max_card + 1) * (max_card + 1);
  double **reach_probs = new double *[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    reach_probs[p] = new double[num_encodings];
    for (unsigned int i = 0; i < num_encodings; ++i) {
      reach_probs[p][i] = 1.0;
    }
  }
  GetReachProbs(base_path, board, prior_bds, reach_probs);
  delete [] prior_bds;

  HandTree *hand_tree = new HandTree(solve_street_, solve_bd, max_street);
  BettingTree *subtree = BettingTree::BuildSubtree(solve_root);
  eg_cfr_->SolveSubgame(subtree, solve_bd, reach_probs, action_sequence,
			hand_tree, nullptr, 1, false, num_its);

  eg_cfr_->Write(subtree, solve_root, target_root, action_sequence, num_its,
		 target_bd);
  
  // Two calls for P1 and P0?
  eg_cfr_->Read(subtree, action_sequence, solve_bd, target_root->Street(),
		true, num_its);
  unsigned int pa = solve_root->PlayerActing();
  br_vals_[0][pa][base_target_nt][solve_bd] =
    eg_cfr_->BRGo(subtree, solve_bd, 0, reach_probs, hand_tree,
		  action_sequence);
  br_vals_[1][pa][base_target_nt][solve_bd] =
    eg_cfr_->BRGo(subtree, solve_bd, 1, reach_probs, hand_tree,
		  action_sequence);
  delete subtree;

#if 0
  double p0_sum_ev, p0_denom, p1_sum_ev, p1_denom;
  eg_cfr_->BestResponse(solve_root, base_solve_nt, solve_bd,
			target_root->Street(), num_its, reach_probs,
			&p0_sum_ev, &p0_denom, &p1_sum_ev, &p1_denom);
  double p0_br = p0_sum_ev / p0_denom;
  double p1_br = p1_sum_ev / p1_denom;
  fprintf(stderr, "P0 BR: %f\n", p0_br);
  fprintf(stderr, "P1 BR: %f\n", p1_br);
  double gap = p0_br + p1_br;
  fprintf(stderr, "Gap: %f\n", gap);
  fprintf(stderr, "Exploitability: %f mbb/g\n", ((gap / 2.0) / 2.0) * 1000.0);
  unsigned int board_ct = BoardTree::BoardCount(street, solve_bd);
  // We don't need to explicitly normalize by the opponent's reach probs,
  // but we do need to normalize by our reach probs.
  p0_br_cum_ += p0_sum_ev * board_ct;
  p1_br_cum_ += p1_sum_ev * board_ct;
  p0_br_sum_weights_ += board_ct * p0_denom;
  p1_br_sum_weights_ += board_ct * p1_denom;
#endif

  delete hand_tree;
  for (unsigned int p = 0; p <= 1; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;
}

void EndgameSolver::Solve(Node *solve_root, Node *target_root,
			  unsigned int solve_bd, unsigned int target_bd,
			  const string &action_sequence,
			  unsigned int base_target_nt,
			  const vector<Node *> *base_path,
			  unsigned int num_its, bool p0, bool p1) {
  if (method_ == ResolvingMethod::CFRD ||
      method_ == ResolvingMethod::MAXMARGIN ||
      method_ == ResolvingMethod::COMBINED) {
    SolveSafe(solve_root, target_root, solve_bd, target_bd, action_sequence,
	      base_path, num_its, p0, p1);
  } else {
    SolveUnsafe(solve_root, target_root, solve_bd, target_bd, 
		base_target_nt, action_sequence, base_path, num_its);
  }
}

// Base tree and endgame tree should correspond exactly up to solve street.
bool EndgameSolver::GetPath(Node *base_node, Node *endgame_node,
			    Node *base_target, Node *endgame_target,
			    vector<Node *> *rev_base_path) {
  if (base_node->Terminal()) return false;
  if (base_node == base_target) {
    if (endgame_node != endgame_target) {
      // fprintf(stderr, "endgame_node != endgame_target?!?\n");
      // exit(-1);
      // This can happen now in case of reentrant base trees.
      return false;
    }
    rev_base_path->push_back(base_node);
    return true;
  }
  // This test has to go after the test above
  // This assumes subgames are rooted at street-initial nodes
  if (base_node->Street() >= solve_street_) return false;
  unsigned int num_succs = base_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (GetPath(base_node->IthSucc(s), endgame_node->IthSucc(s), base_target,
		endgame_target, rev_base_path)) {
      rev_base_path->push_back(base_node);
      return true;
    }
  }
  return false;
}

void EndgameSolver::Solve(Node *solve_root, Node *target_root,
			  Node *base_solve_root, const string &action_sequence,
			  unsigned int solve_bd,
			  unsigned int target_bd, unsigned int base_target_nt,
			  unsigned int num_its,
			  BettingTree *endgame_betting_tree) {
  vector<Node *> rev_base_path, base_path;
  if (! GetPath(betting_tree_->Root(), endgame_betting_tree->Root(),
		base_solve_root, solve_root, &rev_base_path)) {
    fprintf(stderr, "Couldn't find base solve root\n");
    exit(-1);
  }
  unsigned int path_len = rev_base_path.size();
  for (int i = ((int)path_len) - 1; i >= 0; --i) {
    base_path.push_back(rev_base_path[i]);
  }
  Solve(solve_root, target_root, solve_bd, target_bd, action_sequence,
	base_target_nt, &base_path, num_its, true, true);
}
