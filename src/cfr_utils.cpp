// This file contains commonly used CFR routines.  These methods are not
// included in the VCFR class because they have use in CFR implementations
// that do not derive from VCFR (e.g., PCS).

#include <math.h> // lrint()
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
  double half_pot = node->LastBetTo();
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
  // Sign of half_pot reflects who wins the pot
  double half_pot;
  // Player acting encodes player remaining at fold nodes
  // LastBetTo() doesn't include the last called bet
  if (p == node->PlayerActing()) {
    half_pot = node->LastBetTo();
  } else {
    half_pot = -(double)node->LastBetTo();
  }
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

// Abstracted, int sumprobs
void ProcessOppProbsBucketed(Node *node, unsigned int **street_buckets,
			     const CanonicalCards *hands, bool nonneg,
			     unsigned int it, unsigned int soft_warmup,
			     unsigned int hard_warmup, bool update_sumprobs,
			     double *sumprob_scaling, double *opp_probs,
			     double **succ_opp_probs, double *current_probs,
			     int *sumprobs) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int max_card1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double opp_prob = opp_probs[enc];
    if (opp_prob == 0) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	succ_opp_probs[s][enc] = 0;
      }
    } else {
      unsigned int b = street_buckets[st][i];
      double *my_current_probs = current_probs + b * num_succs;
      int *my_sumprobs = nullptr;
      if (sumprobs) my_sumprobs = sumprobs + b * num_succs;
      bool downscale = false;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double succ_opp_prob = opp_prob * my_current_probs[s];
	succ_opp_probs[s][enc] = succ_opp_prob;
	if (update_sumprobs) {
	  if ((hard_warmup == 0 && soft_warmup == 0) ||
	      (soft_warmup > 0 && it <= soft_warmup)) {
	    // Update sumprobs with weight of 1.  Do this when either:
	    // a) There is no warmup (hard or soft), or
	    // b) We are during the soft warmup period.
	    my_sumprobs[s] += lrint(succ_opp_prob * sumprob_scaling[st]);
	  } else if (hard_warmup > 0) {
	    // Use a weight of (it - hard_warmup)
	    my_sumprobs[s] += lrint(succ_opp_prob * (it - hard_warmup) *
				    sumprob_scaling[st]);
	  } else {
	    // Use a weight of (it - soft_warmup)
	    my_sumprobs[s] += lrint(succ_opp_prob * (it - soft_warmup) *
				    sumprob_scaling[st]);
	  }
	  if (my_sumprobs[s] > 2000000000) {
	    downscale = true;
	  }
	}
      }
      if (downscale) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  my_sumprobs[s] /= 2;
	}
      }
    }
  }
}

// Abstracted, double sumprobs
void ProcessOppProbsBucketed(Node *node, unsigned int **street_buckets,
			     const CanonicalCards *hands, bool nonneg,
			     unsigned int it, unsigned int soft_warmup,
			     unsigned int hard_warmup, bool update_sumprobs,
			     double *opp_probs, double **succ_opp_probs,
			     double *current_probs, double *sumprobs) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int max_card1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double opp_prob = opp_probs[enc];
    if (opp_prob == 0) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	succ_opp_probs[s][enc] = 0;
      }
    } else {
      unsigned int b = street_buckets[st][i];
      double *my_current_probs = current_probs + b * num_succs;
      double *my_sumprobs = nullptr;
      if (sumprobs) my_sumprobs = sumprobs + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double succ_opp_prob = opp_prob * my_current_probs[s];
	succ_opp_probs[s][enc] = succ_opp_prob;
	if (update_sumprobs) {
	  if ((hard_warmup == 0 && soft_warmup == 0) ||
	      (soft_warmup > 0 && it <= soft_warmup)) {
	    // Update sumprobs with weight of 1.  Do this when either:
	    // a) There is no warmup (hard or soft), or
	    // b) We are during the soft warmup period.
	    my_sumprobs[s] += succ_opp_prob;
	  } else if (hard_warmup > 0) {
	    // Use a weight of (it - hard_warmup)
	    my_sumprobs[s] += succ_opp_prob * (it - hard_warmup);
	  } else {
	    // Use a weight of (it - soft_warmup)
	    my_sumprobs[s] += succ_opp_prob * (it - soft_warmup);
	  }
	}
      }
    }
  }
}

// Unabstracted, int cs_vals, int sumprobs
void ProcessOppProbs(Node *node, const CanonicalCards *hands, bool bucketed,
		     unsigned int **street_buckets, bool nonneg, bool uniform,
		     double explore, ProbMethod prob_method, unsigned int it,
		     unsigned int soft_warmup, unsigned int hard_warmup,
		     bool update_sumprobs, double *sumprob_scaling,
		     double *opp_probs, double **succ_opp_probs, int *cs_vals,
		     int *sumprobs) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int default_succ_index = node->DefaultSuccIndex();
  unsigned int max_card1 = Game::MaxCard() + 1;
  double *current_probs = new double[num_succs];
  unsigned int num_nonterminal_succs = 0;
  bool *nonterminal_succs = new bool[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (node->IthSucc(s)->Terminal()) {
      nonterminal_succs[s] = false;
    } else {
      nonterminal_succs[s] = true;
      ++num_nonterminal_succs;
    }
  }
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card lo, hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double opp_prob = opp_probs[enc];
    if (opp_prob == 0) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	succ_opp_probs[s][enc] = 0;
      }
    } else {
      int *my_cs_vals;
      if (bucketed) {
	unsigned int b = street_buckets[st][i];
	my_cs_vals = cs_vals + b * num_succs;
      } else {
	my_cs_vals = cs_vals + i * num_succs;
      }
      int *my_sumprobs = nullptr;
      unsigned int b;
      if (update_sumprobs) {
	if (bucketed) {
	  b = street_buckets[st][i];
	  my_sumprobs = sumprobs + b * num_succs;
	} else {
	  my_sumprobs = sumprobs + i * num_succs;
	}
      }
      if (prob_method == ProbMethod::REGRET_MATCHING) {
	RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform,
		       default_succ_index, explore, num_nonterminal_succs,
		       nonterminal_succs, current_probs);
      } else if (prob_method == ProbMethod::PURE) {
	PureProbs(my_cs_vals, num_succs, current_probs);
      } else if (prob_method == ProbMethod::FTL) {
	FTLPureProbs(my_cs_vals, num_succs, current_probs);
      } else {
	fprintf(stderr, "Unknown prob method: %i\n",
		(int)prob_method);
	exit(-1);
      }
      bool downscale = false;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double succ_opp_prob = opp_prob * current_probs[s];
	succ_opp_probs[s][enc] = succ_opp_prob;
	if (update_sumprobs) {
	  if ((hard_warmup == 0 && soft_warmup == 0) ||
	      (soft_warmup > 0 && it <= soft_warmup)) {
	    // Update sumprobs with weight of 1.  Do this when either:
	    // a) There is no warmup (hard or soft), or
	    // b) We are during the soft warmup period.
	    my_sumprobs[s] += lrint(succ_opp_prob * sumprob_scaling[st]);
	  } else if (hard_warmup > 0) {
	    // Use a weight of (it - hard_warmup)
	    my_sumprobs[s] += lrint(succ_opp_prob * (it - hard_warmup) *
				    sumprob_scaling[st]);
	  } else {
	    // Use a weight of (it - soft_warmup)
	    my_sumprobs[s] += lrint(succ_opp_prob * (it - soft_warmup) *
				    sumprob_scaling[st]);
	  }
	  if (my_sumprobs[s] > 2000000000) {
	    downscale = true;
	  }
	}
      }
      if (downscale) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  my_sumprobs[s] /= 2;
	}
      }
    }
  }
  delete [] current_probs;
  delete [] nonterminal_succs;
}

// Unabstracted, double cs_vals, double sumprobs
void ProcessOppProbs(Node *node, const CanonicalCards *hands, bool bucketed,
		     unsigned int **street_buckets, bool nonneg, bool uniform,
		     double explore, unsigned int it, unsigned int soft_warmup,
		     unsigned int hard_warmup, bool update_sumprobs,
		     double *opp_probs, double **succ_opp_probs,
		     double *cs_vals, double *sumprobs) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int default_succ_index = node->DefaultSuccIndex();
  unsigned int max_card1 = Game::MaxCard() + 1;
  double *current_probs = new double[num_succs];
  unsigned int num_nonterminal_succs = 0;
  bool *nonterminal_succs = new bool[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (node->IthSucc(s)->Terminal()) {
      nonterminal_succs[s] = false;
    } else {
      nonterminal_succs[s] = true;
      ++num_nonterminal_succs;
    }
  }
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card lo, hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double opp_prob = opp_probs[enc];
    if (opp_prob == 0) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	succ_opp_probs[s][enc] = 0;
      }
    } else {
      double *my_cs_vals;
      if (bucketed) {
	unsigned int b = street_buckets[st][i];
	my_cs_vals = cs_vals + b * num_succs;
      } else {
	my_cs_vals = cs_vals + i * num_succs;
      }
      double *my_sumprobs = nullptr;
      if (update_sumprobs) {
	if (bucketed) {
	  unsigned int b = street_buckets[st][i];
	  my_sumprobs = sumprobs + b * num_succs;
	} else {
	  my_sumprobs = sumprobs + i * num_succs;
	}
      }
      RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform,
		     default_succ_index, explore, num_nonterminal_succs,
		     nonterminal_succs, current_probs);
      for (unsigned int s = 0; s < num_succs; ++s) {
	double succ_opp_prob = opp_prob * current_probs[s];
	succ_opp_probs[s][enc] = succ_opp_prob;
	if (update_sumprobs) {
	  if ((hard_warmup == 0 && soft_warmup == 0) ||
	      (soft_warmup > 0 && it <= soft_warmup)) {
	    // Update sumprobs with weight of 1.  Do this when either:
	    // a) There is no warmup (hard or soft), or
	    // b) We are during the soft warmup period.
	    my_sumprobs[s] += succ_opp_prob;
	  } else if (hard_warmup > 0) {
	    // Use a weight of (it - hard_warmup)
	    my_sumprobs[s] += succ_opp_prob * (it - hard_warmup);
	  } else {
	    // Use a weight of (it - soft_warmup)
	    my_sumprobs[s] += succ_opp_prob * (it - soft_warmup);
	  }
	}
      }
    }
  }
  delete [] current_probs;
  delete [] nonterminal_succs;
}

// Unabstracted, int cs_vals, double sumprobs
void ProcessOppProbs(Node *node, const CanonicalCards *hands, bool bucketed,
		     unsigned int **street_buckets, bool nonneg, bool uniform,
		     double explore, unsigned int it, unsigned int soft_warmup,
		     unsigned int hard_warmup, bool update_sumprobs,
		     double *opp_probs, double **succ_opp_probs,
		     int *cs_vals, double *sumprobs) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int default_succ_index = node->DefaultSuccIndex();
  unsigned int max_card1 = Game::MaxCard() + 1;
  double *current_probs = new double[num_succs];
  unsigned int num_nonterminal_succs = 0;
  bool *nonterminal_succs = new bool[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (node->IthSucc(s)->Terminal()) {
      nonterminal_succs[s] = false;
    } else {
      nonterminal_succs[s] = true;
      ++num_nonterminal_succs;
    }
  }
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    unsigned int enc;
    if (num_hole_cards == 1) {
      enc = hi;
    } else {
      Card lo = cards[1];
      enc = hi * max_card1 + lo;
    }
    double opp_prob = opp_probs[enc];
    if (opp_prob == 0) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	succ_opp_probs[s][enc] = 0;
      }
    } else {
      int *my_cs_vals;
      if (bucketed) {
	unsigned int b = street_buckets[st][i];
	my_cs_vals = cs_vals + b * num_succs;
      } else {
	my_cs_vals = cs_vals + i * num_succs;
      }
      double *my_sumprobs = nullptr;
      if (update_sumprobs) {
	if (bucketed) {
	  unsigned int b = street_buckets[st][i];
	  my_sumprobs = sumprobs + b * num_succs;
	} else {
	  my_sumprobs = sumprobs + i * num_succs;
	}
      }
      RegretsToProbs(my_cs_vals, num_succs, nonneg, uniform,
		     default_succ_index, explore, num_nonterminal_succs,
		     nonterminal_succs, current_probs);
      for (unsigned int s = 0; s < num_succs; ++s) {
	double succ_opp_prob = opp_prob * current_probs[s];
	succ_opp_probs[s][enc] = succ_opp_prob;
	if (update_sumprobs) {
	  if ((hard_warmup == 0 && soft_warmup == 0) ||
	      (soft_warmup > 0 && it <= soft_warmup)) {
	    // Update sumprobs with weight of 1.  Do this when either:
	    // a) There is no warmup (hard or soft), or
	    // b) We are during the soft warmup period.
	    my_sumprobs[s] += succ_opp_prob;
	  } else if (hard_warmup > 0) {
	    // Use a weight of (it - hard_warmup)
	    my_sumprobs[s] += succ_opp_prob * (it - hard_warmup);
	  } else {
	    // Use a weight of (it - soft_warmup)
	    my_sumprobs[s] += succ_opp_prob * (it - soft_warmup);
	  }
	}
      }
    }
  }
  delete [] current_probs;
  delete [] nonterminal_succs;
}

void DeleteOldFiles(const CardAbstraction &ca, const BettingAbstraction &ba,
		    const CFRConfig &cc, unsigned int it) {
  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  ca.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
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

static void MPTerminalVal(const Card *our_hole_cards, Card *opp_hole_cards,
			  double joint_prob, unsigned int our_hv, bool lost,
			  bool num_chop, unsigned int *choppers, unsigned int p,
			  unsigned int opp, unsigned int *contributions,
			  double **opp_probs, const CanonicalCards *hands,
			  double *showdown_val, double *fold_val,
			  double *sum_weights) {
  if (opp == Game::NumPlayers()) {
    *sum_weights = joint_prob;
    unsigned int num_players = Game::NumPlayers();
    unsigned int sum_contributions = 0;
    for (unsigned int opp = 0; opp < num_players; ++opp) {
      if (opp != p) sum_contributions += contributions[p];
    }
    *fold_val = joint_prob * sum_contributions;
    if (lost) {
      *showdown_val = joint_prob * -(double)contributions[p];
    } else if (num_chop > 0) {
      unsigned int sum_loser_contributions = 0;
      unsigned int num_players = Game::NumPlayers();
      for (unsigned int opp = 0; opp < num_players; ++opp) {
	if (opp == p) continue;
	unsigned int i;
	for (i = 0; i < num_chop; ++i) {
	  if (choppers[i] == opp) break;
	}
	if (i == num_chop) {
	  sum_loser_contributions += contributions[opp];
	}
      }
      *showdown_val = joint_prob *
	(((double)sum_loser_contributions) / (double)(num_chop + 1));
    } else {
      *showdown_val = joint_prob * sum_contributions;
    }
  } else if (opp == p) {
    MPTerminalVal(our_hole_cards, opp_hole_cards, joint_prob, our_hv, lost,
		  num_chop, choppers, p, opp + 1, contributions, opp_probs,
		  hands, showdown_val, fold_val, sum_weights);
  } else {
    unsigned int num_hole_card_pairs = hands->NumRaw();
    unsigned int max_card1 = Game::MaxCard() + 1;
    *showdown_val = 0;
    *fold_val = 0;
    *sum_weights = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *hole_cards = hands->Cards(i);
      Card opp_hi = hole_cards[0];
      if (InCards(opp_hi, our_hole_cards, 2)) continue;
      Card opp_lo = hole_cards[1];
      if (InCards(opp_lo, our_hole_cards, 2)) continue;
      unsigned int enc = opp_hi * max_card1 + opp_lo;
      double this_opp_prob = opp_probs[opp][enc];
      if (this_opp_prob == 0) continue;
      unsigned int opp_hv = hands->HandValue(i);
      bool new_lost = lost || opp_hv > our_hv;
      unsigned int new_num_chop;
      if (new_lost) {
	new_num_chop = 0;
      } else if (opp_hv == our_hv) {
	choppers[num_chop] = opp;
	new_num_chop = num_chop + 1;
      } else {
	new_num_chop = num_chop;
      }
      if (opp > p) {
	opp_hole_cards[(opp - 1) * 2] = opp_hi;
	opp_hole_cards[(opp - 1) * 2 + 1] = opp_lo;
      } else {
	opp_hole_cards[opp * 2] = opp_hi;
	opp_hole_cards[opp * 2 + 1] = opp_lo;
      }
      double new_joint_prob = joint_prob * this_opp_prob;
      double this_showdown_val, this_fold_val, this_sum_weights;
      MPTerminalVal(our_hole_cards, opp_hole_cards, new_joint_prob, our_hv,
		    new_lost, new_num_chop, choppers, p, opp + 1,
		    contributions, opp_probs, hands, &this_showdown_val,
		    &this_fold_val, &this_sum_weights);
      *showdown_val += this_showdown_val;
      *fold_val += this_fold_val;
      *sum_weights += this_sum_weights;
    }
  }
}

// We should take into account who has folded.
void MPTerminal(unsigned int p, const CanonicalCards *hands,
		unsigned int *contributions, double **opp_probs,
		double **showdown_vals, double **fold_vals) {
  unsigned int num_hole_card_pairs = hands->NumRaw();
  *showdown_vals = new double[num_hole_card_pairs];
  *fold_vals = new double[num_hole_card_pairs];
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<Card []> opp_hole_cards(new Card[(num_players - 1) * 2]);
  unique_ptr<unsigned int []> choppers(new unsigned int[num_players - 1]);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    unsigned int our_hv = hands->HandValue(i);
    const Card *our_hole_cards = hands->Cards(i);
    double sum_showdown_vals = 0, sum_fold_vals = 0, sum_weights = 0;
    MPTerminalVal(our_hole_cards, opp_hole_cards.get(), 1.0, our_hv, false, 0,
		  choppers.get(), p, 0, contributions, opp_probs, hands,
		  &sum_showdown_vals, &sum_fold_vals, &sum_weights);
    (*showdown_vals)[i] = sum_showdown_vals / sum_weights;
    (*fold_vals)[i] = sum_fold_vals / sum_weights;
  }
}

