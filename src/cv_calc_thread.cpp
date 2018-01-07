#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
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
#include "constants.h"
#include "cv_calc_thread.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "vcfr_state.h"
#include "vcfr.h"

using namespace std;


CVCalcThread::CVCalcThread(const CardAbstraction &ca,
			   const BettingAbstraction &ba,
			   const CFRConfig &cc, const Buckets &buckets,
			   const BettingTree *betting_tree,
			   unsigned int num_threads,
			   unsigned int it) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  it_ = it;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = false;
  }
  br_current_ = false;
  value_calculation_ = true;
  prune_ = true;

  BoardTree::Create();
  BoardTree::CreateLookup();

  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  }
}

CVCalcThread::~CVCalcThread(void) {
}

// Base tree and endgame tree should correspond exactly up to solve street.
bool GetPath(Node *node, Node *target, vector<Node *> *rev_path) {
  if (node->Terminal()) return false;
  if (node == target) {
    rev_path->push_back(node);
    return true;
  }
  if (node->Street() > target->Street()) return false;
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (GetPath(node->IthSucc(s), target, rev_path)) {
      rev_path->push_back(node);
      return true;
    }
  }
  return false;
}

void CVCalcThread::GetOppReachProbs(Node *node, unsigned int gbd,
				    unsigned int opp, double *opp_probs) {
  vector<Node *> rev_path, path;
  if (! GetPath(betting_tree_->Root(), node, &rev_path)) {
    fprintf(stderr, "Couldn't find path\n");
    exit(-1);
  }
  unsigned int path_len = rev_path.size();
  for (int i = ((int)path_len) - 1; i >= 0; --i) {
    path.push_back(rev_path[i]);
  }

  unsigned int final_st = node->Street();
  unique_ptr<unsigned int []> bds(new unsigned int[final_st + 1]);
  bds[0] = 0;
  bds[final_st] = gbd;
  const Card *board = BoardTree::Board(final_st, gbd);
  for (unsigned int st = 1; st < final_st; ++st) {
    bds[st] = BoardTree::LookupBoard(board, st);
  }
  
  unsigned int max_card1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < path_len - 1; ++i) {
    Node *this_node = path[i];
    if (this_node->PlayerActing() == opp) {
      unsigned int num_succs = this_node->NumSuccs();
      Node *next_node = path[i+1];
      unsigned int s;
      for (s = 0; s < num_succs; ++s) {
	if (this_node->IthSucc(s) == next_node) break;
      }
      if (s == num_succs) {
	fprintf(stderr, "Couldn't connect nodes\n");
	exit(-1);
      }
      unsigned int st = this_node->Street();
      unsigned int nt = this_node->NonterminalID();
      unsigned int dsi = this_node->DefaultSuccIndex();
      if (buckets_.None(st)) {
	fprintf(stderr, "Assume buckets\n");
	exit(-1);
      }
      unsigned int bd = bds[st];
      unique_ptr<double []> probs(new double [num_succs]);
      const CanonicalCards *hands;
      if (st < final_st) {
	hands = prior_hand_tree_->Hands(st, bd);
      } else {
	hands = hand_tree_->Hands(final_st, 0);
      }
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
	const Card *cards = hands->Cards(j);
	Card hi = cards[0];
	Card lo = cards[1];
	unsigned int enc = hi * max_card1 + lo;
	unsigned int h = bd * num_hole_card_pairs + j;
	unsigned int b = buckets_.Bucket(st, h);
	unsigned int offset = b * num_succs;
	trunk_sumprobs_->Probs(opp, st, nt, offset, num_succs, dsi,
			       probs.get());
	opp_probs[enc] *= probs[s];
      }
    }
  }
}

// Need to get opp reach probs.
void CVCalcThread::Go(Node *node, unsigned int bd) {
  time_t start_t = time(NULL);
  unsigned int max_street = Game::MaxStreet();
  unsigned int st = node->Street();

  bool street_initial = (node->IthSucc(0)->Street() == node->Street());
  
  unique_ptr<bool []> trunk_streets(new bool[max_street + 1]);
  unique_ptr<bool []> subgame_streets(new bool[max_street + 1]);
  for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
    if (st1 < st) {
      trunk_streets[st1] = true;
    } else if (st1 == st && ! street_initial) {
      trunk_streets[st1] = true;
    } else {
      trunk_streets[st1] = false;
    }
    subgame_streets[st1] = (st1 >= st);
  }
  
  // Want sumprobs for both players.
  trunk_sumprobs_.reset(new CFRValues(nullptr, true, trunk_streets.get(),
				      betting_tree_, 0, 0, card_abstraction_,
				      buckets_.NumBuckets(),
				      compressed_streets_));

  subgame_sumprobs_.reset(new CFRValues(nullptr, true, subgame_streets.get(),
				      betting_tree_, 0, 0, card_abstraction_,
				      buckets_.NumBuckets(),
				      compressed_streets_));
  
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  fprintf(stderr, "Reading trunk sumprobs\n");
  trunk_sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
  fprintf(stderr, "Read trunk sumprobs\n");

  fprintf(stderr, "Reading subgame sumprobs\n");
  unique_ptr<unsigned int []>
    num_full_holdings(new unsigned int[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    // Assume all streets bucketed
    num_full_holdings[st] = buckets_.NumBuckets(st);
  }
  subgame_sumprobs_->ReadSubtreeFromFull(dir, it_, betting_tree_->Root(),
					 node, node, "x",
					 num_full_holdings.get(), kMaxUInt);
  fprintf(stderr, "Read subgame sumprobs\n");

  bucketed_ = false;
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (! buckets_.None(st)) bucketed_ = true;
  }

  hand_tree_ = new HandTree(st, bd, max_street);
  prior_hand_tree_ = new HandTree(0, 0, st-1);

  unsigned int pa = node->PlayerActing();
  double *opp_probs = AllocateOppProbs(true);
  GetOppReachProbs(node, bd, pa^1, opp_probs);
  unsigned int **street_buckets = AllocateStreetBuckets();
  unsigned int max_card1 = Game::MaxCard() + 1;
  // Wait, this never gets deleted
  double *total_card_probs = new double[max_card1];
  VCFRState state(opp_probs, total_card_probs, hand_tree_, st, 0, "",
		  bd, st, street_buckets, pa, nullptr,
		  subgame_sumprobs_.get());
  SetStreetBuckets(st, bd, state);
  double *vals = Process(node, 0, state, st);
  DeleteStreetBuckets(street_buckets);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  const Card *board = BoardTree::Board(st, bd);
  unsigned int num_board_cards = Game::NumBoardCards(st);
  const CanonicalCards *hands = hand_tree_->Hands(st, 0);
#if 0
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    unsigned int enc = cards[0] * max_card1 + cards[1];
    OutputNCards(board, num_board_cards);
    printf(" / ");
    OutputTwoCards(cards);
    printf(" opp prob %f (i %u)\n", opp_probs[enc], i);
  }
#endif
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    double sum_opp_probs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      if (opp_cards[0] == cards[0] || opp_cards[0] == cards[1] ||
	  opp_cards[1] == cards[0] || opp_cards[1] == cards[1]) {
	continue;
      }
      unsigned int enc = opp_cards[0] * max_card1 + opp_cards[1];
      sum_opp_probs += opp_probs[enc];
    }
    OutputNCards(board, num_board_cards);
    printf(" / ");
    OutputTwoCards(cards);
    printf(" %f (i %u) (%f/%f)\n", vals[i] / sum_opp_probs, i,
	   vals[i], sum_opp_probs);
  }
  fflush(stdout);
  delete [] opp_probs;
  delete [] vals;
  delete [] total_card_probs;
  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  printf("Process took %.1f seconds\n", diff_sec);
  fflush(stdout);
  delete hand_tree_;
  delete prior_hand_tree_;
  hand_tree_ = nullptr;
  prior_hand_tree_ = nullptr;
}
