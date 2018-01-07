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
//
// We still write out values for individual hands.  So does this code do
// anything different than cfr_thread.cpp?  Not sure it does.

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
#include "vcfr_state.h"
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

  if (trunk) {
    // Should handle asymmetric systems
    // Should honor sumprobs_streets_
    sumprobs_.reset(new CFRValues(nullptr, true, nullptr, betting_tree_, 0, 0,
				  card_abstraction_, buckets_.NumBuckets(),
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
  } else {
    // We are not the trunk thread
    sumprobs_.reset(nullptr);
  }
}

BCFRThread::~BCFRThread(void) {
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

double *BCFRThread::OurChoice(Node *node, unsigned int lbd,
			      const VCFRState &state) {
  unsigned int st = node->Street();
  
  double *vals = VCFR::OurChoice(node, lbd, state);
  unsigned int gbd = 0;
  if (st > 0) {
    gbd = BoardTree::GlobalIndex(state.RootBdSt(), state.RootBd(), st, lbd);
  }
  WriteValues(node, gbd, state.ActionSequence(), vals);
  
  return vals;
}

// Can't skip succ even if succ_sum_opp_probs is zero.  I need to write
// out values at every node.
double *BCFRThread::OppChoice(Node *node, unsigned int lbd, 
			      const VCFRState &state) {
  double *vals = VCFR::OppChoice(node, lbd, state);

  unsigned int st = node->Street();
  unsigned int gbd = 0;
  if (st > 0) {
    gbd = BoardTree::GlobalIndex(state.RootBdSt(), state.RootBd(), st, lbd);
  }
  WriteValues(node, gbd, state.ActionSequence(), vals);

  return vals;
}

void BCFRThread::Go(void) {
  time_t start_t = time(NULL);

  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);

  double *opp_probs = AllocateOppProbs(true);
  unsigned int **street_buckets = AllocateStreetBuckets();
  VCFRState state(opp_probs, street_buckets, trunk_hand_tree_, p_, nullptr,
		  sumprobs_.get());
  SetStreetBuckets(0, 0, state);
  double *vals = Process(betting_tree_->Root(), 0, state, 0);
  DeleteStreetBuckets(street_buckets);
  delete [] opp_probs;

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

  delete [] vals;

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  fprintf(stderr, "Processing took %.1f seconds\n", diff_sec);
}
