// #include <math.h>
#include <stdio.h>
#include <stdlib.h>
// #include <string.h>
// #include <unistd.h>

// #include <algorithm>
// #include <vector>

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
#include "mp_rgbr.h"
#include "split.h"
#include "vcfr.h"

using namespace std;

MPRGBR::MPRGBR(const CardAbstraction &ca, const BettingAbstraction &ba,
	       const CFRConfig &cc, const Buckets &buckets,
	       const BettingTree *betting_tree, bool current,
	       unsigned int num_threads, const bool *streets) :
  MPVCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  br_current_ = current;
  value_calculation_ = true;

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

  hand_tree_ = new HandTree(0, 0, max_street);
}

MPRGBR::~MPRGBR(void) {
  delete hand_tree_;
}

static unsigned int PrecedingPlayer(unsigned int p) {
  if (p == 0) return Game::NumPlayers() - 1;
  else        return p - 1;
}

double MPRGBR::Go(unsigned int it, unsigned int p) {
  it_ = it;
  p_ = p;

  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Can't handle asymmetric yet\n");
    exit(-1);
#if 0
    char buf[100];
    sprintf(buf, ".p%u", target_p_);
    strcat(dir, buf);
#endif
  }

  bool *streets = nullptr;
  unsigned int max_street = Game::MaxStreet();

  bool all_streets = true;
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (! best_response_streets_[st]) all_streets = false;
  }
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    players[p] = p != p_ || ! all_streets;
  }
  if (br_current_) {
    regrets_.reset(new CFRValues(players.get(), false, streets, betting_tree_,
				 0, 0, card_abstraction_, buckets_,
				 compressed_streets_));
    regrets_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
    sumprobs_.reset(nullptr);
  } else {
    sumprobs_.reset(new CFRValues(players.get(), true, streets, betting_tree_,
				  0, 0, card_abstraction_, buckets_,
				  compressed_streets_));
    sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
    regrets_.reset(nullptr);
  }

  unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
  bool bucketed = false;
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucketed_streets[st] = ! buckets_.None(st);
    if (bucketed_streets[st]) bucketed = true;
  }
  if (bucketed) {
    // Current strategy always uses doubles
    // Want opponent's strategy
    // This doesn't generalize to multiplayer
    current_strategy_.reset(new CFRValues(players.get(), false,
					  bucketed_streets.get(),
					  betting_tree_, 0, 0,
					  card_abstraction_, buckets_,
					  compressed_streets_));
    current_strategy_->AllocateAndClearDoubles(betting_tree_->Root(),
					       kMaxUInt);
  } else {
    current_strategy_.reset(nullptr);
  }

  delete [] streets;

  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card = Game::MaxCard();
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card + 1;
  else                     num_enc = (max_card + 1) * (max_card + 1);
  double **opp_probs = new double *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    if (p == p_) {
      opp_probs[p] = nullptr;
    } else {
      opp_probs[p] = new double[num_enc];
      for (unsigned int i = 0; i < num_enc; ++i) opp_probs[p][i] = 1.0;
    }
  }

  if (current_strategy_.get() != nullptr) {
    SetCurrentStrategy(betting_tree_->Root());
  }

  unsigned int **street_buckets = InitializeStreetBuckets();

  unsigned int last_bet_to = Game::BigBlind();
  unique_ptr<unsigned int []> contributions(new unsigned int[num_players]);
  unsigned int big_blind_p = PrecedingPlayer(Game::FirstToAct(0));
  unsigned int small_blind_p = PrecedingPlayer(big_blind_p);
  for (unsigned int p = 0; p < num_players; ++p) {
    if (p == small_blind_p) {
      contributions[p] = Game::SmallBlind();
    } else if (p == big_blind_p) {
      contributions[p] = Game::BigBlind();
    } else {
      contributions[p] = 0;
    }
  }
  double *vals = Process(betting_tree_->Root(), 0, last_bet_to,
			 contributions.get(), opp_probs, street_buckets, "x",
			 0);
  DeleteStreetBuckets(street_buckets);
  
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);

  unsigned int num_remaining = Game::NumCardsInDeck() -
    Game::NumCardsForStreet(0);
  unsigned int num_opp_hole_card_pairs =
    num_remaining * (num_remaining - 1) / 2;
  double sum = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    sum += vals[i];
  }
  double overall = sum / (num_hole_card_pairs * num_opp_hole_card_pairs);
  delete [] vals;
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] opp_probs[p];
  }
  delete [] opp_probs;

  regrets_.reset(nullptr);
  sumprobs_.reset(nullptr);

  return overall;
}
