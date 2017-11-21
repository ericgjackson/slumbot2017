// Do a single pass of Vanilla, calculating CBRs for every single hand.
// CBRs are counterfactual best response values.
//
// CBRs are written out in normal hand order on every street except max
// street.  On the final street, CBRs are written out for hands as sorted by
// hand strength.
//
// CBRs for a hand depend only on the opponent's strategy above and below
// a given node.  P1's CBRs are distinct from P2's CBRs.
//
// Right now run_cfrp produces trunk sumprobs files for the preflop and
// flop, and compressed sumprobs files for the turn and river.  I can't
// easily split on the flop now if I want each file of sumprobs to belong
// to a single thread.  So split on the turn instead?
//
// Eventually I might want to switch to the same form of multithreading
// as run_cfrp and run_cfrp_rgbr.  In other words, have a bunch of
// subgame objects that get spawned.
//
// Splitting is not supported right now.  Code currently assumes that
// kSplitStreet is the same as the split street used in running CFR.  If
// I really want that, use SplitStreet parameter.  But then the question
// might be why not go all the way and use subgames just like CFR.
//
// On the other hand, this form of splitting may be simpler.

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
#include "cbr_thread.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "vcfr_state.h"
#include "vcfr.h"

using namespace std;

CBRThread::CBRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
		     const CFRConfig &cc, const Buckets &buckets,
		     const BettingTree *betting_tree, bool cfrs,
		     unsigned int p, HandTree *trunk_hand_tree,
		     unsigned int num_threads, unsigned int it) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  cfrs_ = cfrs;
  p_ = p;
  trunk_hand_tree_ = trunk_hand_tree;
  it_ = it;
  regrets_.reset(nullptr);


  // final_hand_vals_ = nullptr;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = ! cfrs_;
  }
  br_current_ = false;
  value_calculation_ = true;
  // Can't skip succ even if succ_sum_opp_probs is zero.  I need to write
  // out CBR values at every node.
  prune_ = false;

  // Always want sumprobs for the opponent(s).  Want sumprobs for target player
  // as well when computing CFRs (as opposed to CBRs).
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    players[p] = cfrs_ || p != p_;
  }

  // Should handle asymmetric systems
  // Should honor sumprobs_streets_
  sumprobs_.reset(new CFRValues(players.get(), true, nullptr, betting_tree_,
				0, 0, card_abstraction_, buckets_,
				compressed_streets_));
  
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    if (cfrs_) {
      fprintf(stderr, "Don't support CFRs and asymmetric\n");
      exit(-1);
    }
    char buf[100];
    sprintf(buf, ".p%u", p_^1);
    strcat(dir, buf);
  }
  sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);

  bucketed_ = false;
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (! buckets_.None(st)) bucketed_ = true;
  }
  
#if 0
  unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucketed_streets[st] = ! buckets_.None(st);
    if (bucketed_streets[st]) bucketed_ = true;
  }
  if (bucketed_) {
    // Current strategy always uses doubles
    // Shouldn't have to create this.
    current_strategy_.reset(new CFRValues(players.get(), false,
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
#endif
}

CBRThread::~CBRThread(void) {
}

void CBRThread::WriteValues(Node *node, unsigned int gbd,
			    const string &action_sequence, double *vals) {
  char dir[500], dir2[500], buf[500];
  unsigned int street = node->Street();
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s",
	  Files::NewCFRBase(), Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    if (cfrs_) {
      fprintf(stderr, "Don't support CFRs and asymmetric\n");
      exit(-1);
    }
    char buf2[100];
    sprintf(buf2, ".p%u", p_);
    strcat(dir, buf2);
  }
  sprintf(dir2, "%s/%s.%u.p%u/%s", dir, cfrs_ ? "cfrs" : "cbrs",
	  it_, p_,  action_sequence.c_str());
  Mkdir(dir2);
  sprintf(buf, "%s/vals.%u", dir2, gbd);
  Writer writer(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    writer.WriteFloat((float)vals[i]);
  }
}

double *CBRThread::OurChoice(Node *node, unsigned int lbd, 
			     const VCFRState &state) {
  double *vals = VCFR::OurChoice(node, lbd, state);

  unsigned int st = node->Street();
  unsigned int gbd = 0;
  if (st > 0) {
    gbd = BoardTree::GlobalIndex(state.RootBdSt(), state.RootBd(), st, lbd);
  }
  WriteValues(node, gbd, state.ActionSequence(), vals);
  
  return vals;
}

double *CBRThread::OppChoice(Node *node, unsigned int lbd, 
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

double CBRThread::Go(void) {
  time_t start_t = time(NULL);
  double *opp_probs = AllocateOppProbs(true);
  unsigned int **street_buckets = AllocateStreetBuckets();
  VCFRState state(opp_probs, street_buckets, trunk_hand_tree_, p_);
  SetStreetBuckets(0, 0, state);
  double *vals = Process(betting_tree_->Root(), 0, state, 0);
  DeleteStreetBuckets(street_buckets);
  delete [] opp_probs;
  // EVs for our hands are summed over all opponent hole card pairs.  To
  // compute properly normalized EV, need to divide by that number.
  unsigned int num_cards_in_deck = Game::NumCardsInDeck();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_remaining = num_cards_in_deck - num_hole_cards;
  unsigned int num_opp_hole_card_pairs;
  if (num_hole_cards == 1) {
    num_opp_hole_card_pairs = num_remaining;
  } else {
    num_opp_hole_card_pairs = num_remaining * (num_remaining - 1) / 2;
  }
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  double sum = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    sum += vals[i] / num_opp_hole_card_pairs;
  }
  double ev = sum / num_hole_card_pairs;
  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  printf("Process took %.1f seconds\n", diff_sec);
  fflush(stdout);
  delete [] vals;
  return ev;
}
