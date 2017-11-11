// This is an implementation of CFR+.
//
// TODO: Remove old trunk files.
// TODO: skip unreachable subgames.  Will need to copy unaltered regret and
//       sumprob files to new iteration.  Subgame only unreachable if not
//       reachable for *any* flop board.  That may not be very common.  Could
//       also write out 0/1 for each flop board for reachability.  What does
//       Alberta do?
// TODO: skip river if no opp hands reach.  How does Alberta do it?

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
#include "cfrp.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "split.h"
#include "vcfr.h"

using namespace std;

CFRP::CFRP(const CardAbstraction &ca, const BettingAbstraction &ba,
	   const CFRConfig &cc, const Buckets &buckets,
	   const BettingTree *betting_tree, unsigned int num_threads,
	   unsigned int target_p) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  if (betting_abstraction_.Asymmetric()) {
    target_p_ = target_p;
  }
  unsigned int max_street = Game::MaxStreet();

  BoardTree::Create();

  HandValueTree::Create();

  hand_tree_ = new HandTree(0, 0, max_street);

  bool *streets = nullptr;
  if (subgame_street_ <= max_street) {
    streets = new bool[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      streets[st] = st < subgame_street_;
    }
  }  
  regrets_.reset(new CFRValues(nullptr, false, streets, betting_tree_, 0,
			       0, card_abstraction_, buckets_,
			       compressed_streets_));
  
  // Should honor sumprobs_streets_
  if (betting_abstraction_.Asymmetric()) {
    unsigned int num_players = Game::NumPlayers();
    unique_ptr<bool []> players(new bool [num_players]);
    for (unsigned int p = 0; p < num_players; ++p) players[p] = false;
    players[target_p_] = true;
    sumprobs_.reset(new CFRValues(players.get(), true, streets, betting_tree_,
				  0, 0, card_abstraction_, buckets_,
				  compressed_streets_));
  } else {
    sumprobs_.reset(new CFRValues(nullptr, true, streets, betting_tree_, 0,
				  0, card_abstraction_, buckets_,
				  compressed_streets_));
  }

  unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
  bucketed_ = false;
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucketed_streets[st] = ! buckets_.None(st);
    if (bucketed_streets[st]) bucketed_ = true;
  }
  if (bucketed_) {
    // Hmm, we only want to allocate this for the streets that need it
    current_strategy_.reset(new CFRValues(nullptr, false,
					  bucketed_streets.get(),
					  betting_tree_, 0, 0,
					  card_abstraction_, buckets_,
					  compressed_streets_));
  } else {
    current_strategy_.reset(nullptr);
  }
  
  delete [] streets;

  it_ = 0;
  if (subgame_street_ <= max_street) {
    // Currently don't want to prune in the trunk when running CFR+ in the
    // trunk.  If we do this, then no regret files get written out for
    // unreached subgames.  And then we fail when we try to read those regret
    // files on the next iteration.
    //
    // A better (faster) solution might be to be robust to this situation.
    // We could copy the regret files from it N to it N+1 when the subgame
    // is not reached on iteration N+1.
    prune_ = false;
  }
}

CFRP::~CFRP(void) {
  delete hand_tree_;
}

void CFRP::FloorRegrets(Node *node) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  if (node->PlayerActing() == p_ && ! buckets_.None(st) && num_succs > 1) {
    unsigned int nt = node->NonterminalID();
    unsigned int num_buckets = buckets_.NumBuckets(st);
    unsigned int num_values = num_buckets * num_succs;
    if (nn_regrets_ && regret_floors_[st] >= 0) {
      if (regrets_->Ints(p_, st)) {
	int *i_all_regrets;
	regrets_->Values(p_, st, nt, &i_all_regrets);
	int floor = regret_floors_[st];
	for (unsigned int i = 0; i < num_values; ++i) {
	  if (i_all_regrets[i] < floor) i_all_regrets[i] = floor;
	}
      } else {
	double *d_all_regrets;
	regrets_->Values(p_, st, nt, &d_all_regrets);
	double floor = regret_floors_[st];
	for (unsigned int i = 0; i < num_values; ++i) {
	  if (d_all_regrets[i] < floor) d_all_regrets[i] = floor;
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    FloorRegrets(node->IthSucc(s));
  }
}

// Do trunk in main thread
void CFRP::HalfIteration(unsigned int p) {
  p_ = p;
  fprintf(stderr, "%s half iteration\n", p_ ? "P1" : "P2");
  if (current_strategy_.get() != nullptr) {
    SetCurrentStrategy(betting_tree_->Root());
  }

  if (subgame_street_ <= Game::MaxStreet()) {
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

  if (subgame_street_ <= Game::MaxStreet()) pre_phase_ = true;
  double *opp_probs = AllocateOppProbs(true);
  unsigned int **street_buckets = AllocateStreetBuckets();
  VCFRState state(opp_probs, street_buckets, hand_tree_);
  SetStreetBuckets(0, 0, state);
  double *vals = Process(betting_tree_->Root(), 0, state, 0);
  if (subgame_street_ <= Game::MaxStreet()) {
    delete [] vals;
    WaitForFinalSubgames();
    pre_phase_ = false;
    vals = Process(betting_tree_->Root(), 0, state, 0);
  }
  DeleteStreetBuckets(street_buckets);
  delete [] opp_probs;
#if 0
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    fprintf(stderr, "vals[%u] %f\n", i, vals[i]);
  }
#endif

#if 0
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_opp_hole_card_pairs = 50 * 49 / 2;
  double sum_vals = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    sum_vals += vals[i] / num_opp_hole_card_pairs;
  }
  double avg_val = sum_vals / num_hole_card_pairs;
  fprintf(stderr, "%s avg val %f\n", p1 ? "P1" : "P2", avg_val);
#endif

  delete [] vals;

  if (nn_regrets_ && bucketed_) {
    FloorRegrets(betting_tree_->Root());
  }
}

void CFRP::Checkpoint(unsigned int it) {
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
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
  Mkdir(dir);
  regrets_->Write(dir, it, betting_tree_->Root(), "x", kMaxUInt);
  sumprobs_->Write(dir, it, betting_tree_->Root(), "x", kMaxUInt);
}

void CFRP::ReadFromCheckpoint(unsigned int it) {
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
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
  regrets_->Read(dir, it, betting_tree_->Root(), "x", kMaxUInt);
  sumprobs_->Read(dir, it, betting_tree_->Root(), "x", kMaxUInt);
}

void CFRP::Run(unsigned int start_it, unsigned int end_it) {
  if (start_it == 0) {
    fprintf(stderr, "CFR starts from iteration 1\n");
    exit(-1);
  }
  DeleteOldFiles(card_abstraction_, betting_abstraction_, cfr_config_, end_it);

  if (start_it > 1) {
    ReadFromCheckpoint(start_it - 1);
    last_checkpoint_it_ = start_it - 1;
  } else {
    if (double_regrets_) {
      regrets_->AllocateAndClearDoubles(betting_tree_->Root(), kMaxUInt);
    } else {
      regrets_->AllocateAndClearInts(betting_tree_->Root(), kMaxUInt);
    }
    if (double_sumprobs_) {
      sumprobs_->AllocateAndClearDoubles(betting_tree_->Root(), kMaxUInt);
    } else {
      sumprobs_->AllocateAndClearInts(betting_tree_->Root(), kMaxUInt);
    }
  }
  if (bucketed_) {
    // Current strategy always uses doubles
    current_strategy_->AllocateAndClearDoubles(betting_tree_->Root(),
					       kMaxUInt);
  }

  if (subgame_street_ <= Game::MaxStreet()) {
    prune_ = false;
  }
  
  for (it_ = start_it; it_ <= end_it; ++it_) {
    fprintf(stderr, "It %u\n", it_);
    HalfIteration(1);
    HalfIteration(0);
  }

  Checkpoint(end_it);
}
