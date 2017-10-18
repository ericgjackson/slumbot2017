// BCFR values are "bucketed counterfactual values".  The code here generates
// bucket-level CFR values whereas cbr_thread.cpp generates card-level CFR
// values.
//
// The BCFR values are written out in normal hand order on every street except
// the final street.  On the final street, the values are written out for hands
// as sorted by hand strength.
//
// The values for a hand depend only on the opponent's strategy above and below
// a given node, and our strategy below the node.  P0's values are distinct
// from P1's values.

#include <stdio.h>
#include <stdlib.h>

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
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "bcfr_thread.h"
#include "vcfr.h"

using namespace std;

BCFRThread::BCFRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
		       const CFRConfig &cc, const Buckets &buckets,
		       const BettingTree *betting_tree, unsigned int p,
		       HandTree *trunk_hand_tree, unsigned int thread_index,
		       unsigned int num_threads, unsigned int it,
		       BCFRThread **threads, bool trunk) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  p_ = p;
  trunk_hand_tree_ = trunk_hand_tree;
  thread_index_ = thread_index;
  it_ = it;
  threads_ = threads;

  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = false;
  }
  br_current_ = false;
  value_calculation_ = true;
  prune_ = false;

  regrets_.reset(nullptr);
  if (trunk) {
    root_bd_st_ = 0;
    root_bd_ = 0;
    hand_tree_ = trunk_hand_tree_;
    // Should handle asymmetric systems
    // Should honor sumprobs_streets_
    sumprobs_.reset(new CFRValues(nullptr, true, nullptr, betting_tree_, 0, 0,
				  card_abstraction_, buckets_,
				  compressed_streets_));

    char dir[500];
    sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(), Game::NumPlayers(),
	    card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	    Game::NumSuits(), Game::MaxStreet(),
	    betting_abstraction_.BettingAbstractionName().c_str(),
	    cfr_config_.CFRConfigName().c_str());
#if 0
    if (betting_abstraction_.Asymmetric()) {
      if (target_p_) strcat(dir, ".p1");
      else           strcat(dir, ".p2");
    }
#endif
    sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);

    unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
    bucketed_ = false;
    for (unsigned int st = 0; st <= max_street; ++st) {
      bucketed_streets[st] = ! buckets_.None(st);
      if (bucketed_streets[st]) bucketed_ = true;
    }
    if (bucketed_) {
      // Current strategy always uses doubles
      // This doesn't generalize to multiplayer
      current_strategy_.reset(new CFRValues(nullptr, false,
					    bucketed_streets.get(),
					    betting_tree_, 0, 0,
					    card_abstraction_, buckets_,
					    compressed_streets_));
      current_strategy_->AllocateAndClearDoubles(betting_tree_->Root(),
						 kMaxUInt);
      SetCurrentStrategy(betting_tree_->Root());
    } else {
      current_strategy_.reset(nullptr);
    }
  } else {
    // We are not the trunk thread
    root_bd_st_ = kSplitStreet;
    root_bd_ = kMaxUInt;
    hand_tree_ = nullptr;
    sumprobs_.reset(nullptr);
  }
}

BCFRThread::~BCFRThread(void) {
  // Don't delete hand_tree_.  In the trunk it is identical to trunk_hand_tree_
  // which is owned by the caller (PRBRBuilder).  In the endgames it is
  // deleted in AfterSplit().
}

void BCFRThread::WriteValues(Node *node, unsigned int gbd,
			     const string &action_sequence, double *vals) {
  char dir[500], buf[500];
  unsigned int street = node->Street();
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s/bcfrs.%u.p%u/%s",
	  Files::NewCFRBase(), Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str(), it_, p_,
	  action_sequence.c_str());
  Mkdir(dir);  
  sprintf(buf, "%s/vals.%u", dir, gbd);
  Writer writer(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    writer.WriteFloat((float)vals[i]);
  }
}

double *BCFRThread::OurChoice(Node *node, unsigned int lbd, double *opp_probs,
			      double sum_opp_probs, double *total_card_probs,
			      unsigned int **street_buckets,
			      const string &action_sequence) {
  unsigned int st = node->Street();
  
  double *vals = VCFR::OurChoice(node, lbd, opp_probs, sum_opp_probs,
				 total_card_probs, street_buckets,
				 action_sequence);
  unsigned int gbd = 0;
  if (st > 0) gbd = BoardTree::GlobalIndex(root_bd_st_, root_bd_, st, lbd);
  WriteValues(node, gbd, action_sequence, vals);
  
  return vals;
}

// Can't skip succ even if succ_sum_opp_probs is zero.  I need to write
// out values at every node.
double *BCFRThread::OppChoice(Node *node, unsigned int lbd, double *opp_probs,
			      double sum_opp_probs, double *total_card_probs,
			      unsigned int **street_buckets,
			      const string &action_sequence) {
  double *vals = VCFR::OppChoice(node, lbd, opp_probs, sum_opp_probs,
				 total_card_probs, street_buckets,
				 action_sequence);

  unsigned int st = node->Street();
  unsigned int gbd = 0;
  if (st > 0) gbd = BoardTree::GlobalIndex(root_bd_st_, root_bd_, st, lbd);
  WriteValues(node, gbd, action_sequence, vals);

  return vals;
}

// Used for the first and third passes
void BCFRThread::Go(void) {
  time_t start_t = time(NULL);

  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int max_card = Game::MaxCard();
  unsigned int num;
  if (num_hole_cards == 1) num = max_card + 1;
  else                     num = (max_card + 1) * (max_card + 1);
  double *opp_probs = new double[num];
  for (unsigned int i = 0; i < num; ++i) opp_probs[i] = 1.0;
  const CanonicalCards *hands = hand_tree_->Hands(0, 0);

  double sum_opp_probs;
  double *total_card_probs = new double[num_hole_card_pairs];
  CommonBetResponseCalcs(0, hands, opp_probs, &sum_opp_probs,
			 total_card_probs);
  unsigned int **street_buckets = InitializeStreetBuckets();
  double *vals = Process(betting_tree_->Root(), 0, opp_probs, sum_opp_probs,
			 total_card_probs, street_buckets, "x", 0);
  DeleteStreetBuckets(street_buckets);
  delete [] total_card_probs;

  // EVs for our hands are summed over all opponent hole card pairs.  To
  // compute properly normalized EV, need to divide by that number.
  unsigned int num_cards_in_deck = Game::NumCardsInDeck();
  unsigned int num_remaining = num_cards_in_deck - num_hole_cards;
  unsigned int num_opp_hole_card_pairs;
  if (num_hole_cards == 1) {
    num_opp_hole_card_pairs = num_remaining;
  } else {
    num_opp_hole_card_pairs = num_remaining * (num_remaining - 1) / 2;
  }
  double sum = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    sum += vals[i] / num_opp_hole_card_pairs;
  }
  double ev = sum / num_hole_card_pairs;
  printf("EV: %f\n", ev);
  fflush(stdout);

  delete [] opp_probs;
  delete [] vals;

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  fprintf(stderr, "Processing took %.1f seconds\n", diff_sec);
}
