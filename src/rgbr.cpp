// I think I could make RGBR derive from CFRP.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
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
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "rgbr.h"
#include "split.h"
#include "vcfr_state.h"
#include "vcfr.h"

using namespace std;

RGBR::RGBR(const CardAbstraction &ca, const BettingAbstraction &ba,
	   const CFRConfig &cc, const Buckets &buckets,
	   const BettingTree *betting_tree, bool current,
	   unsigned int num_threads, const bool *streets,
	   bool always_call_preflop) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  br_current_ = current;
  value_calculation_ = true;
  always_call_preflop_ = always_call_preflop;

  unsigned int max_street = Game::MaxStreet();
  if (streets) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      best_response_streets_[st] = streets[st];
    }
  } else {
    for (unsigned int st = 0; st <= max_street; ++st) {
      best_response_streets_[st] = true;
    }
  }

  BoardTree::Create();
  HandValueTree::Create();

  if (subgame_street_ <= max_street) {
    hand_tree_ = new HandTree(0, 0, subgame_street_ - 1);
  } else {
    hand_tree_ = new HandTree(0, 0, max_street);
  }
}

RGBR::~RGBR(void) {
  delete hand_tree_;
}

#if 0
double *RGBR::Process(Node *node, unsigned int lbd, double *opp_probs,
		      double sum_opp_probs, double *total_card_probs,
		      unsigned int last_st) {
  // Read method for CFRValues assumes that if we pass in a river node,
  // then we are reading a river subtree.  The subtree street is encoded in
  // the filename.
  // More significantly, all of the river sumprobs are in one file.  Not
  // possible to read just the sumprobs for a river subtree.
  unsigned int st = node->Street();
  if (st == Game::MaxStreet() && st > last_st) {
    char dir[500];
    sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(), Game::NumPlayers(),
	    card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	    Game::NumSuits(), Game::MaxStreet(),
	    betting_abstraction_.BettingAbstractionName().c_str(),
	    cfr_config_.CFRConfigName().c_str());
    sumprobs_->Read(dir, it_, node, node->NonterminalID(), kMaxUInt);
  }
  return VCFR::Process(node, lbd, opp_probs, sum_opp_probs, total_card_probs,
		       "", last_st);
}
#endif

double RGBR::Go(unsigned int it, unsigned int p) {
  it_ = it;
  // If P0 is the best-responder, then we will want sumprobs generated in
  // the P1 CFR run.
  target_p_ = p^1;

  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", target_p_);
    strcat(dir, buf);
  }

  bool *streets = nullptr;
  unsigned int max_street = Game::MaxStreet();
  if (subgame_street_ <= max_street) {
    streets = new bool[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      streets[st] = st < subgame_street_;
    }
  }  

  bool all_streets = true;
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (! best_response_streets_[st]) all_streets = false;
  }
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p1 = 0; p1 < num_players; ++p1) {
    players[p1] = p1 != p || ! all_streets;
  }
  if (br_current_) {
    regrets_.reset(new CFRValues(players.get(), false, streets, betting_tree_,
				 0, 0, card_abstraction_,
				 buckets_.NumBuckets(), compressed_streets_));
    regrets_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
    sumprobs_.reset(nullptr);
  } else {
    sumprobs_.reset(new CFRValues(players.get(), true, streets, betting_tree_,
				  0, 0, card_abstraction_,
				  buckets_.NumBuckets(), compressed_streets_));
    sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
    regrets_.reset(nullptr);
  }

  unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
  bucketed_ = false;
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucketed_streets[st] = ! buckets_.None(st);
    if (bucketed_streets[st]) bucketed_ = true;
  }

  delete [] streets;

  if (subgame_street_ <= max_street) {
    // subgame_running_ should be false for all threads
    // active_subgames_ should be nullptr for all threads
    for (unsigned int t = 0; t < num_threads_; ++t) {
      int ret = sem_post(&available_);
      if (ret != 0) {
	fprintf(stderr, "sem_post failed\n");
	exit(-1);
      }
    }
  }

  if (subgame_street_ <= max_street) pre_phase_ = true;
  double *opp_probs = AllocateOppProbs(true);
  unsigned int **street_buckets = AllocateStreetBuckets();
  VCFRState state(opp_probs, street_buckets, hand_tree_, p, regrets_.get(),
		  sumprobs_.get());
  SetStreetBuckets(0, 0, state);
  double *vals = Process(betting_tree_->Root(), 0, state, 0);
  if (subgame_street_ <= max_street) {
    delete [] vals;
    WaitForFinalSubgames();
    pre_phase_ = false;
    vals = Process(betting_tree_->Root(), 0, state, 0);
  }
  DeleteStreetBuckets(street_buckets);
  delete [] opp_probs;
  
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);

#if 0
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *hole_cards = hands->Cards(i);
    OutputTwoCards(hole_cards);
    printf(" %f (i %u)\n", vals[i], i);
  }
  fflush(stdout);
#endif
  // fprintf(stderr, "num nt %u\n", betting_tree_->NumNonterminals(0, 3));
  
  unsigned int num_remaining = Game::NumCardsInDeck() -
    Game::NumCardsForStreet(0);
  unsigned int num_opp_hole_card_pairs =
    num_remaining * (num_remaining - 1) / 2;
  double sum = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    sum += vals[i];
#if 0
    // I don't know why I had this hear.  Maybe when I was displaying
    // per hand values.
    vals[i] /= num_opp_hole_card_pairs;
#endif
  }
  double overall = sum / (num_hole_card_pairs * num_opp_hole_card_pairs);
  delete [] vals;

  regrets_.reset(nullptr);
  sumprobs_.reset(nullptr);

  return overall;
}
