// This file contains commonly used CFR routines.  These methods are not
// included in the VCFR class because they have use in CFR implementations
// that do not derive from VCFR (e.g., PCS).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "files.h"
#include "game.h"
#include "io.h"
#include "split.h"

double *Showdown(Node *node, const CanonicalCards *hands, double *opp_probs,
		 double sum_opp_probs, double *total_card_probs) {
  unsigned int max_card1 = Game::MaxCard() + 1;
  double cum_prob = 0;
  double cum_card_probs[52];
  for (Card c = 0; c < max_card1; ++c) cum_card_probs[c] = 0;
  unsigned int num_hole_card_pairs = hands->NumRaw();
  double *win_probs = new double[num_hole_card_pairs];
  double half_pot = (node->PotSize() / 2);
  double *vals = new double[num_hole_card_pairs];

  unsigned int j = 0;
  while (j < num_hole_card_pairs) {
    unsigned int last_hand_val = hands->HandValue(j);
    unsigned int begin_range = j;
    // Make three passes through the range of equally strong hands
    // First pass computes win counts for each hand and finds end of range
    // Second pass updates cumulative counters
    // Third pass computes lose counts for each hand
    while (j < num_hole_card_pairs) {
      unsigned int hand_val = hands->HandValue(j);
      if (hand_val != last_hand_val) break;
      const Card *cards = hands->Cards(j);
      Card hi = cards[0];
      Card lo = cards[1];
      win_probs[j] = cum_prob - cum_card_probs[hi] - cum_card_probs[lo];
      ++j;
    }
    // Positions begin_range...j-1 (inclusive) all have the same hand value
    for (unsigned int k = begin_range; k < j; ++k) {
      const Card *cards = hands->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int code = hi * max_card1 + lo;
      double prob = opp_probs[code];
      cum_card_probs[hi] += prob;
      cum_card_probs[lo] += prob;
      cum_prob += prob;
    }
    for (unsigned int k = begin_range; k < j; ++k) {
      const Card *cards = hands->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      double better_hi_prob = total_card_probs[hi] - cum_card_probs[hi];
      double better_lo_prob = total_card_probs[lo] - cum_card_probs[lo];
      double lose_prob = (sum_opp_probs - cum_prob) -
	better_hi_prob - better_lo_prob;
      vals[k] = (win_probs[k] - lose_prob) * half_pot;
    }
  }

  delete [] win_probs;

  return vals;
}

double *Fold(Node *node, unsigned int p, const CanonicalCards *hands,
	     double *opp_probs, double sum_opp_probs,
	     double *total_card_probs) {
  unsigned int max_card1 = Game::MaxCard() + 1;
  double half_pot = (node->PotSize() / 2);
  bool we_fold = (p == node->PlayerFolding());
  // Sign of half_pot reflects who folds
  if (we_fold) half_pot = -half_pot;
  unsigned int num_hole_card_pairs = hands->NumRaw();
  double *vals = new double[num_hole_card_pairs];

  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    double opp_prob = opp_probs[enc];
    vals[i] = half_pot *
      (sum_opp_probs + opp_prob -
       (total_card_probs[hi] + total_card_probs[lo]));
  }

  return vals;
}

void CommonBetResponseCalcs(unsigned int st, const CanonicalCards *hands,
			    double *opp_probs, double *ret_sum_opp_probs,
			    double *total_card_probs) {
  double sum_opp_probs = 0;
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  Card max_card = Game::MaxCard();
  for (Card c = 0; c <= max_card; ++c) total_card_probs[c] = 0;

  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * (max_card + 1) + lo;
    double opp_prob = opp_probs[enc];
    sum_opp_probs += opp_prob;
    total_card_probs[hi] += opp_prob;
    total_card_probs[lo] += opp_prob;
  }
  *ret_sum_opp_probs = sum_opp_probs;
}

void DeleteOldFiles(const CardAbstraction &ca, const BettingAbstraction &ba,
		    const CFRConfig &cc, unsigned int it) {
  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(), ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  ba.BettingAbstractionName().c_str(), cc.CFRConfigName().c_str());

  if (! FileExists(dir)) return;
  
  vector<string> listing;
  GetDirectoryListing(dir, &listing);
  unsigned int num_listing = listing.size();
  unsigned int num_deleted = 0;
  for (unsigned int i = 0; i < num_listing; ++i) {
    string full_path = listing[i];
    unsigned int full_path_len = full_path.size();
    int j = full_path_len - 1;
    while (j > 0 && full_path[j] != '/') --j;
    if (strncmp(full_path.c_str() + j + 1, "sumprobs", 8) == 0 ||
	strncmp(full_path.c_str() + j + 1, "regrets", 7) == 0) {
      string filename(full_path, j + 1, full_path_len - (j + 1));
      vector<string> comps;
      Split(filename.c_str(), '.', false, &comps);
      if (comps.size() != 8) {
	fprintf(stderr, "File \"%s\" has wrong number of components\n",
		full_path.c_str());
	exit(-1);
      }
      unsigned int file_it;
      if (sscanf(comps[5].c_str(), "%u", &file_it) != 1) {
	fprintf(stderr, "Couldn't extract iteration from file \"%s\"\n",
		full_path.c_str());
	exit(-1);
      }
      if (file_it == it) {
	RemoveFile(full_path.c_str());
	++num_deleted;
      }
    }
  }
  fprintf(stderr, "%u files deleted\n", num_deleted);
}
