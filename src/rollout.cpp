// I thought maybe there was a bug.  On postflop streets, we gather WML
// statistics for every hole card pair whether canonical or not.  Do the
// suits of the cards encode information about future cards to come?
// Oh, this is only true if we only iterate through canonical boards.  But
// for the postflop streets we iterate through all raw boards.  For the
// preflop we iterate through only the canonical boards so I had to worry
// about this.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "board_tree.h"
#include "canonical_cards.h"
#include "cards.h"
#include "constants.h"
#include "game.h"
#include "hand_value_tree.h"
#include "rollout.h"
#include "sorting.h"

using namespace std;

static short *RiverHandStrength(const Card *board, bool wins) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_board_cards = Game::NumBoardCards(max_street);

  vector<unsigned int> vb(num_board_cards);
  for (unsigned int i = 0; i < num_board_cards; ++i) vb[i] = board[i];
  sort(vb.begin(), vb.end());
  unsigned int *ui_board = new unsigned int[num_board_cards];
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    ui_board[(num_board_cards - 1) - i] = vb[i];
  }
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(max_street);
  vector< pair<unsigned int, unsigned int> > v(num_hole_card_pairs);
  unsigned int max_card = Game::MaxCard();
  unsigned int hole_cards[2];
  unsigned int hcp = 0;
  for (unsigned int hi = 1; hi <= max_card; ++hi) {
    if (InCards(hi, board, num_board_cards)) continue;
    hole_cards[0] = hi;
    for (unsigned int lo = 0; lo < hi; ++lo) {
      if (InCards(lo, board, num_board_cards)) continue;
      hole_cards[1] = lo;
      unsigned int hv = HandValueTree::Val(ui_board, hole_cards);
      unsigned int enc = hi * (max_card + 1) + lo;
      v[hcp] = make_pair(hv, enc);
      ++hcp;
    }
  }
  delete [] ui_board;
  sort(v.begin(), v.end(), g_puiui_lower_compare);

  unsigned int num_enc = (max_card + 1) * (max_card + 1);
  short *values = new short[num_enc];
  // Necessary?
  for (unsigned int i = 0; i < num_enc; ++i) values[i] = 32000;
  unsigned int num_cards_in_deck = Game::NumCardsInDeck();
  // The number of possible hole card pairs containing a given card
  unsigned int num_buddies = (num_cards_in_deck - num_board_cards) - 1;
  unsigned int *seen = new unsigned int[max_card + 1];
  for (unsigned int i = 0; i <= max_card; ++i) seen[i] = 0;
  unsigned int *beats = new unsigned int[num_hole_card_pairs];
  unsigned int last_hv = kMaxUInt;
  unsigned int j = 0;
  while (j < num_hole_card_pairs) {
    unsigned int begin_range = j;
    // Make three passes through the range of equally strong hands
    // First pass computes win counts for each hand and finds end of range
    // Second pass updates cumulative counters
    // Third pass computes lose counts for each hand
    while (j < num_hole_card_pairs) {
      unsigned int hv = v[j].first;
      unsigned int enc = v[j].second;
      unsigned int hi = enc / (max_card + 1);
      unsigned int lo = enc % (max_card + 1);
      if (hv != last_hv) {
	last_hv = hv;
	if (j != 0) break;
      }
      beats[j] = begin_range - seen[hi] - seen[lo];
      ++j;
    }
    // Positions begin_range...j-1 (inclusive) all have the same hand value
    for (unsigned int k = begin_range; k < j; ++k) {
      unsigned int enc = v[k].second;
      unsigned int hi = enc / (max_card + 1);
      unsigned int lo = enc % (max_card + 1);
      ++seen[hi];
      ++seen[lo];
    }
    if (wins) {
      for (unsigned int k = begin_range; k < j; ++k) {
	unsigned int enc = v[k].second;
	values[enc] = (short)beats[k];
      }
    } else {
      unsigned short base_lose = num_hole_card_pairs - j;
      for (unsigned int k = begin_range; k < j; ++k) {
	unsigned int enc = v[k].second;
	unsigned int hi = enc / (max_card + 1);
	unsigned int lo = enc % (max_card + 1);
	// With five-card boards, there should be 46 hole card pairs containing,
	// say, Kc.  52 cards - 5 on board - Kc
	short lose = base_lose -
	  ((num_buddies - seen[hi]) + (num_buddies - seen[lo]));
	// beats[k] and lose are the two values we care about
	// beats[k] - lose is the WML
	short wml = ((short)beats[k]) - lose;
	values[enc] = wml;
	// OutputTwoCards(hi, lo);
	// printf(" %i %i %i %u\n", (int)wml, beats[k], (int)lose, enc);
      }
    }
  }
  delete [] seen;
  delete [] beats;
  return values;
}

static void GetPercentiles(vector<short> &wmls, double *percentiles,
			   unsigned int num_percentiles,
			   short *my_percentiles) {
  sort(wmls.begin(), wmls.end());
  unsigned int num = wmls.size();
  for (unsigned int i = 0; i < num_percentiles; ++i) {
    double percentile = percentiles[i];
    unsigned int j = percentile * (num - 1) + 0.5;
    if (j >= num) {
      fprintf(stderr, "OOB pct %f j %u num %u\n", percentile, j, num);
      exit(-1);
    }
    short wml = wmls[j];
    my_percentiles[i] = wml;
  }
}

// We need to pool the WMLs for all the variants of each canonical hand.
// What about for the flop/turn/river?
static short *ComputePreflopPercentiles(double *percentiles,
					unsigned int num_percentiles,
					bool wins) {
  BoardTree::BuildBoardCounts();
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_ms_board_cards = Game::NumBoardCards(max_street);
  // Num cards left after board cards and hole cards for target player
  // removed from deck.
  unsigned int num_remaining = Game::NumCardsInDeck() - num_ms_board_cards -
    Game::NumCardsForStreet(0);
  // Assume two hole cards
  int max_wml = num_remaining * (num_remaining - 1) / 2;
  int num_wmls = 2 * max_wml + 1;
  unsigned int max_card = Game::MaxCard();
  unsigned int num_enc = (max_card + 1) * (max_card + 1);
  unsigned int **wml_counts = new unsigned int *[num_enc];
  for (unsigned int enc = 0; enc < num_enc; ++enc) {
    wml_counts[enc] = new unsigned int[num_wmls];
    for (int w = 0; w < num_wmls; ++w) wml_counts[enc][w] = 0;
  }
  unsigned int num_boards = BoardTree::NumBoards(max_street);
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    if (bd % 1000 == 0) fprintf(stderr, "bd %u/%u\n", bd, num_boards);
    const Card *board = BoardTree::Board(max_street, bd);
    unsigned int board_count = BoardTree::BoardCount(max_street, bd);
    short *river_wmls = RiverHandStrength(board, wins);
    for (unsigned int enc = 0; enc < num_enc; ++enc) {
      short wml = river_wmls[enc];
      if (wml != 32000) {
	// The raw WML values can be negative.  They range from -990 to 990,
	// I think, for full-deck holdem.  We normalize them to the range
	// 0 to 1980.
	int norm_wml = wml + max_wml;
	wml_counts[enc][norm_wml] += board_count;
      }
    }
    delete [] river_wmls;
  }
  CanonicalCards preflop_hands(2, NULL, 0, 0, false);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_vals = num_hole_card_pairs * num_percentiles;
  short *pct_vals = new short[num_vals];
  // Take all the counts for non-canonical hands and add them to the
  // counts for the canonical hands.
  unsigned int hcp = 0;
  for (unsigned int hi = 1; hi <= max_card; ++hi) {
    for (unsigned int lo = 0; lo < hi; ++lo) {
      unsigned int enc = hi * (max_card + 1) + lo;
      if (preflop_hands.NumVariants(hcp) == 0) {
	unsigned int canon_enc = preflop_hands.Canon(hcp);
	for (int w = 0; w < num_wmls; ++w) {
	  wml_counts[canon_enc][w] += wml_counts[enc][w];
	  wml_counts[enc][w] = 0;
	}
      }
      ++hcp;
    }
  }

  hcp = 0;
  unsigned int *canon_enc_to_hcp = new unsigned int[num_enc];
  for (unsigned int hi = 1; hi <= max_card; ++hi) {
    for (unsigned int lo = 0; lo < hi; ++lo) {
      if (preflop_hands.NumVariants(hcp) > 0) {
	unsigned int enc = hi * (max_card + 1) + lo;
	canon_enc_to_hcp[enc] = hcp;
	unsigned int *counts = wml_counts[enc];
	unsigned int sum_counts = 0;
	for (int w = 0; w < num_wmls; ++w) sum_counts += counts[w];
	unsigned int p = 0, cum = 0;
	int w = 0;
	while (p < num_percentiles && w < num_wmls) {
	  double pct = percentiles[p];
	  unsigned int threshold = pct * sum_counts;
	  cum += counts[w];
	  if (cum >= threshold) {
	    // Undo the normalization
	    pct_vals[hcp * num_percentiles + p] = (short)(w - max_wml);
	    OutputTwoCards(hi, lo);
	    printf(" %f %i\n", pct, w - max_wml);
	    fflush(stdout);
	    ++p;
	  }
	  ++w;
	}
	// Should get to the last percentile before the end of the WMLs
	if (p < num_percentiles) {
	  fprintf(stderr, "p %u expected %u\n", p, num_percentiles);
	}
      }
      ++hcp;
    }
  }

  for (unsigned int enc = 0; enc < num_enc; ++enc) {
    delete [] wml_counts[enc];
  }
  delete [] wml_counts;

  // Copy the percentile values from the canonical hands to the
  // non-canonical ones.
  hcp = 0;
  for (unsigned int hi = 1; hi <= max_card; ++hi) {
    for (unsigned int lo = 0; lo < hi; ++lo) {
      if (preflop_hands.NumVariants(hcp) == 0) {
	unsigned int canon_hcp = canon_enc_to_hcp[preflop_hands.Canon(hcp)];
	for (unsigned int p = 0; p < num_percentiles; ++p) {
	  pct_vals[hcp * num_percentiles + p] =
	    pct_vals[canon_hcp * num_percentiles + p];
	}
      }
      ++hcp;
    }
  }

  delete [] canon_enc_to_hcp;

  return pct_vals;
}

// Not practical for preflop for full-deck holdem.
static vector<short> *ComputeRollout(Card *board, bool wins, unsigned int st) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int max_card = Game::MaxCard();
  unsigned int num_enc = (max_card + 1) * (max_card + 1);
  vector<short> *wmls = new vector<short>[num_enc];
  if (st == max_street) {
    short *wmls1 = RiverHandStrength(board, wins);
    for (unsigned int i = 0; i < num_enc; ++i) {
      short wml = wmls1[i];
      if (wml != 32000) {
	wmls[i].push_back(wml);
      }
    }
    delete [] wmls1;
  } else {
    unsigned int num_board_cards = Game::NumBoardCards(st);
    unsigned int nst = st + 1;
    unsigned int num_new_board_cards = Game::NumCardsForStreet(nst);
    if (num_new_board_cards == 1) {
      for (unsigned int c = 0; c <= max_card; ++c) {
	if (InCards(c, board, num_board_cards)) continue;
	board[num_board_cards] = c;
	vector<short> *next_wmls = ComputeRollout(board, wins, nst);
	for (unsigned int i = 0; i < num_enc; ++i) {
	  vector<short> &v = next_wmls[i];
	  unsigned int num = v.size();
	  for (unsigned int j = 0; j < num; ++j) {
	    wmls[i].push_back(v[j]);
	  }
	}
	delete [] next_wmls;
      }
    } else if (num_new_board_cards == 3) {
      unsigned int hi, mid, lo;
      for (hi = 2; hi <= max_card; ++hi) {
	fprintf(stderr, "hi %u/%u\n", hi, max_card + 1);
	if (InCards(hi, board, num_board_cards)) continue;
	board[num_board_cards] = hi;
	for (mid = 1; mid < hi; ++mid) {
	  if (InCards(mid, board, num_board_cards)) continue;
	  board[num_board_cards + 1] = mid;
	  for (lo = 0; lo < mid; ++lo) {
	    board[num_board_cards + 2] = lo;
	    vector<short> *next_wmls = ComputeRollout(board, wins, nst);
	    for (unsigned int i = 0; i < num_enc; ++i) {
	      vector<short> &v = next_wmls[i];
	      unsigned int num = v.size();
	      for (unsigned int j = 0; j < num; ++j) {
		wmls[i].push_back(v[j]);
	      }
	    }
	    delete [] next_wmls;
	  }
	}
      }
    } else {
      fprintf(stderr, "Unhandled number of new board cards: %u\n",
	      num_new_board_cards);
      exit(-1);
    }
  }
  return wmls;
}

// On turn, deal out all river cards.  Compute WML for all hands.
short *ComputeRollout(unsigned int st, double *percentiles,
		      unsigned int num_percentiles,
		      double squashing, bool wins) {
  unsigned int num_boards = BoardTree::NumBoards(st);
  unsigned int num_board_cards = Game::NumBoardCards(st);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int num_hands = num_boards * num_hole_card_pairs;
  unsigned int num_vals = num_hands * num_percentiles;
  short *pct_vals;
  if (st == 0) {
    pct_vals = ComputePreflopPercentiles(percentiles, num_percentiles, wins);
  } else {
    pct_vals = new short[num_vals];
    Card board[5];
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      fprintf(stderr, "bd %u/%u\n", bd, num_boards);
      const Card *st_board = BoardTree::Board(st, bd);
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	board[i] = st_board[i];
      }
      unsigned int h = bd * num_hole_card_pairs;
      vector<short> *wmls = ComputeRollout(board, wins, st);
      unsigned int max_card = Game::MaxCard();
      unsigned int num_enc = (max_card + 1) * (max_card + 1);
      for (unsigned int i = 0; i < num_enc; ++i) {
	vector<short> &v = wmls[i];
	if (v.size() == 0) continue;
	GetPercentiles(v, percentiles, num_percentiles,
		       &pct_vals[h * num_percentiles]);
	++h;
      }
      delete [] wmls;
    }
  }
  if (squashing == 1.0) {
    return pct_vals;
  }
  short min_val = 32700;
  short max_val = -32700;
  for (unsigned int i = 0; i < num_vals; ++i) {
    short val = pct_vals[i];
    if (val < min_val) min_val = val;
    if (val > max_val) max_val = val;
  }
  // Renormalize vals so that worse hands have higher values and the lowest
  // value is zero.  Then squash by raising to the given power.
  for (unsigned int i = 0; i < num_vals; ++i) {
    short val = pct_vals[i];
    short norm_val = -(val - max_val);
    pct_vals[i] = (short)pow(norm_val, squashing);
  }
  return pct_vals;
}
