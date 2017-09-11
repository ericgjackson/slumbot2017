// We copy the Vanilla CFR code from vcfr.cpp into DynamicCBR, albeit
// with a lot of simplifications.  We only support a best response, for
// example.  Maybe we could go back to using VCFR at least if we made a
// few changes to the VCFR code.  We might need to pass in a sumprobs
// object, for example.

#include <stdio.h>
#include <stdlib.h>

#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "dynamic_cbr.h"
#include "eg_cfr.h"
#include "game.h"
#include "hand_tree.h"

DynamicCBR::DynamicCBR(void) {
  unsigned int max_street = Game::MaxStreet();
  street_buckets_ = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    street_buckets_[st] = new unsigned int[num_hole_card_pairs];
  }
}

DynamicCBR::~DynamicCBR(void) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    delete [] street_buckets_[st];
  }
  delete [] street_buckets_;
}

double *DynamicCBR::OurChoice(Node *node, unsigned int lbd,
			      unsigned int p, double *opp_probs,
			      double sum_opp_probs,
			      double *total_card_probs,
			      HandTree *hand_tree, const CFRValues *sumprobs,
			      unsigned int root_bd_st,
			      unsigned int root_bd,
			      const Buckets &buckets,
			      const CardAbstraction &card_abstraction) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  double *vals = new double[num_hole_card_pairs];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
  double **succ_vals = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    succ_vals[s] = Process(node->IthSucc(s), lbd, p, opp_probs,
			   sum_opp_probs, total_card_probs, hand_tree,
			   sumprobs, root_bd_st, root_bd, buckets,
			   card_abstraction, st);
  }
  
  if (cfrs_) {
    // Will need card abstraction and buckets for the endgame strategy.  The
    // complication is that this could either be a strategy read from the
    // base or computed by endgame solving.
    unsigned int nt = node->NonterminalID();
    bool bucketed = ! buckets.None(st) &&
      node->PotSize() < card_abstraction.BucketThreshold(st);
    
    double *d_sumprobs = nullptr;
    int *i_sumprobs = nullptr;
    if (sumprobs->Ints(p, st)) {
      int *i_all_sumprobs;
      sumprobs->Values(p, st, nt, &i_all_sumprobs);
      if (bucketed) {
	i_sumprobs = i_all_sumprobs;
      } else {
	i_sumprobs = i_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
      }
    } else {
      double *d_all_sumprobs;
      sumprobs->Values(p, st, nt, &d_all_sumprobs);
      if (bucketed) {
	d_sumprobs = d_all_sumprobs;
      } else {
	d_sumprobs = d_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
      }
    }

    unique_ptr<double []> current_probs(new double[num_succs]);
    unsigned int default_succ_index = node->DefaultSuccIndex();
    bool nonneg = true;
    
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      if (i_sumprobs) {
	int *my_sumprobs;
	if (bucketed) {
	  unsigned int b = street_buckets_[st][i];
	  my_sumprobs = i_sumprobs + b * num_succs;
	} else {
	  my_sumprobs = i_sumprobs + i * num_succs;
	}
	RegretsToProbs(my_sumprobs, num_succs, nonneg, false,
		       default_succ_index, 0, 0, nullptr, current_probs.get());
      } else if (d_sumprobs) {
	double *my_sumprobs;
	if (bucketed) {
	  unsigned int b = street_buckets_[st][i];
	  my_sumprobs = d_sumprobs + b * num_succs;
	} else {
	  my_sumprobs = d_sumprobs + i * num_succs;
	}
	RegretsToProbs(my_sumprobs, num_succs, nonneg, false,
		       default_succ_index, 0, 0, nullptr, current_probs.get());
      } else {
	fprintf(stderr, "No sumprobs?!?  st %u p %u nt %u\n", st, p, nt);
	exit(-1);
      }
      vals[i] = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	vals[i] += succ_vals[s][i] * current_probs[s];
      }
    }
  } else {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      double max_val = succ_vals[0][i];
      for (unsigned int s = 1; s < num_succs; ++s) {
	double sv = succ_vals[s][i];
	if (sv > max_val) max_val = sv;
      }
      vals[i] = max_val;
    }
  }
  
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_vals[s];
  }
  delete [] succ_vals;

  return vals;
}

double *DynamicCBR::OppChoice(Node *node, unsigned int lbd,
			      unsigned int p, double *opp_probs,
			      double sum_opp_probs,
			      double *total_card_probs,
			      HandTree *hand_tree, const CFRValues *sumprobs,
			      unsigned int root_bd_st,
			      unsigned int root_bd,
			      const Buckets &buckets,
			      const CardAbstraction &card_abstraction) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int nt = node->NonterminalID();
  unsigned int opp = p^1;

  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc;
  if (num_hole_cards == 1) num_enc = max_card1;
  else                     num_enc = max_card1 * max_card1;
  double **succ_opp_probs = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    succ_opp_probs[s] = new double[num_enc];
    for (unsigned int i = 0; i < num_enc; ++i) succ_opp_probs[s][i] = 0;
  }

  // Will need card abstraction and buckets for the endgame strategy.  The
  // complication is that this could either be a strategy read from the
  // base or computed by endgame solving.
  bool bucketed = ! buckets.None(st) &&
    node->PotSize() < card_abstraction.BucketThreshold(st);

  double *d_sumprobs = nullptr;
  int *i_sumprobs = nullptr;
  if (sumprobs->Ints(opp, st)) {
    int *i_all_sumprobs;
    sumprobs->Values(opp, st, nt, &i_all_sumprobs);
    if (bucketed) {
      i_sumprobs = i_all_sumprobs;
    } else {
      i_sumprobs = i_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
    }
  } else {
    double *d_all_sumprobs;
    sumprobs->Values(opp, st, nt, &d_all_sumprobs);
    if (bucketed) {
      d_sumprobs = d_all_sumprobs;
    } else {
      d_sumprobs = d_all_sumprobs + lbd * num_hole_card_pairs * num_succs;
    }
  }

  unsigned int default_succ_index = node->DefaultSuccIndex();
  bool nonneg = true;
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  unique_ptr<double []> current_probs(new double[num_succs]);

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
      if (i_sumprobs) {
	int *my_sumprobs;
	if (bucketed) {
	  unsigned int b = street_buckets_[st][i];
	  my_sumprobs = i_sumprobs + b * num_succs;
	} else {
	  my_sumprobs = i_sumprobs + i * num_succs;
	}
	RegretsToProbs(my_sumprobs, num_succs, nonneg, false,
		       default_succ_index, 0, 0, nullptr, current_probs.get());
      } else if (d_sumprobs) {
	double *my_sumprobs;
	if (bucketed) {
	  unsigned int b = street_buckets_[st][i];
	  my_sumprobs = d_sumprobs + b * num_succs;
	} else {
	  my_sumprobs = d_sumprobs + i * num_succs;
	}
	RegretsToProbs(my_sumprobs, num_succs, nonneg, false,
		       default_succ_index, 0, 0, nullptr, current_probs.get());
      } else {
	fprintf(stderr, "No sumprobs?!?  st %u opp %u nt %u\n", st, opp, nt);
	exit(-1);
      }
      for (unsigned int s = 0; s < num_succs; ++s) {
	double succ_opp_prob = opp_prob * current_probs[s];
	succ_opp_probs[s][enc] = succ_opp_prob;
      }
    }
  }

  double *vals = NULL;
  double *succ_total_card_probs = new double[num_hole_card_pairs];
  double succ_sum_opp_probs;
  for (unsigned int s = 0; s < num_succs; ++s) {
    CommonBetResponseCalcs(st, hands, succ_opp_probs[s], &succ_sum_opp_probs,
			   succ_total_card_probs);
    // Safe to prune
    if (succ_sum_opp_probs == 0) {
      continue;
    }
    double *succ_vals = Process(node->IthSucc(s), lbd, p, succ_opp_probs[s],
				succ_sum_opp_probs, succ_total_card_probs,
				hand_tree, sumprobs, root_bd_st, root_bd,
				buckets, card_abstraction, st);
    if (vals == NULL) {
      vals = succ_vals;
    } else {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	vals[i] += succ_vals[i];
      }
      delete [] succ_vals;
    }
  }
  if (vals == NULL) {
    // This can happen if there were non-zero opp probs on the prior street,
    // but the board cards just dealt blocked all the opponent hands with
    // non-zero probability.
    vals = new double[num_hole_card_pairs];
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) vals[i] = 0;
  }
  delete [] succ_total_card_probs;
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_opp_probs[s];
  }
  delete [] succ_opp_probs;

  return vals;
}

// Initialize buckets for this street
void DynamicCBR::SetStreetBuckets(unsigned int st, unsigned int gbd,
				  const Buckets &buckets,
				  const CanonicalCards *hands) {
  if (! buckets.None(st)) {
    unsigned int max_street = Game::MaxStreet();
    unsigned int num_board_cards = Game::NumBoardCards(st);
    unique_ptr<Card []> cards(new Card[num_board_cards + 2]);
    const Card *board = BoardTree::Board(st, gbd);
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      cards[i + 2] = board[i];
    }
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int h;
      if (st == max_street) {
	// Hands on final street were reordered by hand strength, but
	// bucket lookup requires the unordered hole card pair index
	const Card *hole_cards = hands->Cards(i);
	cards[0] = hole_cards[0];
	cards[1] = hole_cards[1];
	unsigned int hcp = HCPIndex(st, cards.get());
	h = gbd * num_hole_card_pairs + hcp;
      } else {
	h = gbd * num_hole_card_pairs + i;
      }
      street_buckets_[st][i] = buckets.Bucket(st, h);
    }
  }
}

double *DynamicCBR::StreetInitial(Node *node, unsigned int plbd,
				  unsigned int p, double *opp_probs,
				  HandTree *hand_tree,
				  const CFRValues *sumprobs,
				  unsigned int root_bd_st,
				  unsigned int root_bd,
				  const Buckets &buckets,
				  const CardAbstraction &card_abstraction) {
  unsigned int nst = node->Street();
  unsigned int pst = nst - 1;
  unsigned int prev_num_hole_card_pairs = Game::NumHoleCardPairs(pst);
  const CanonicalCards *pred_hands = hand_tree->Hands(pst, plbd);
  Card max_card = Game::MaxCard();
  unsigned int num_encodings = (max_card + 1) * (max_card + 1);
  unsigned int *prev_canons = new unsigned int[num_encodings];
  double *vals = new double[prev_num_hole_card_pairs];
  for (unsigned int i = 0; i < prev_num_hole_card_pairs; ++i) vals[i] = 0;
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) > 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_encoding = prev_cards[0] * (max_card + 1) +
	prev_cards[1];
      prev_canons[prev_encoding] = ph;
    }
  }
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      const Card *prev_cards = pred_hands->Cards(ph);
      unsigned int prev_encoding = prev_cards[0] * (max_card + 1) +
	prev_cards[1];
      unsigned int pc = prev_canons[pred_hands->Canon(ph)];
      prev_canons[prev_encoding] = pc;
    }
  }
  unsigned int pgbd;
  if (root_bd_st == 0) {
    pgbd = plbd;
  } else {
    pgbd = BoardTree::GlobalIndex(root_bd_st, root_bd, pst, plbd);
  }
  unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
  unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
  for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
    unsigned int nlbd;
    if (root_bd_st == 0) {
      nlbd = ngbd;
    } else {
      nlbd = BoardTree::LocalIndex(root_bd_st, root_bd, nst, ngbd);
    }

    const CanonicalCards *hands = hand_tree->Hands(nst, nlbd);
    SetStreetBuckets(nst, ngbd, buckets, hands);
    
    // I can pass unset values for sum_opp_probs and total_card_probs.  I
    // know I will come across an opp choice node before getting to a terminal
    // node.
    double *next_vals = Process(node, nlbd, p, opp_probs, 0, nullptr,
				hand_tree, sumprobs, root_bd_st,
				root_bd, buckets, card_abstraction, nst);

    unsigned int board_variants = BoardTree::NumVariants(nst, ngbd);
    unsigned int num_next_hands = hands->NumRaw();
    for (unsigned int nh = 0; nh < num_next_hands; ++nh) {
      const Card *cards = hands->Cards(nh);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int encoding = hi * (max_card + 1) + lo;
      unsigned int prev_canon = prev_canons[encoding];
      vals[prev_canon] += board_variants * next_vals[nh];
    }
    delete [] next_vals;
  }
  // Scale down the values of the previous-street canonical hands
  double scale_down = Game::StreetPermutations(nst);
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    unsigned int prev_hand_variants = pred_hands->NumVariants(ph);
    if (prev_hand_variants > 0) {
      // Is this doing the right thing?
      vals[ph] /= scale_down * prev_hand_variants;
    }
  }
  // Copy the canonical hand values to the non-canonical
  for (unsigned int ph = 0; ph < prev_num_hole_card_pairs; ++ph) {
    if (pred_hands->NumVariants(ph) == 0) {
      vals[ph] = vals[prev_canons[pred_hands->Canon(ph)]];
    }
  }

  delete [] prev_canons;
  return vals;
}

double *DynamicCBR::Process(Node *node, unsigned int lbd,
			    unsigned int p, double *opp_probs,
			    double sum_opp_probs, double *total_card_probs,
			    HandTree *hand_tree, const CFRValues *sumprobs,
			    unsigned int root_bd_st, unsigned int root_bd,
			    const Buckets &buckets,
			    const CardAbstraction &card_abstraction,
			    unsigned int last_st) {
  unsigned int st = node->Street();
  if (node->Terminal()) {
    if (node->Showdown()) {
      double *vals = Showdown(node, hand_tree->Hands(st, lbd), opp_probs,
			      sum_opp_probs, total_card_probs);
      return vals;
    } else {
      double *vals = Fold(node, p, hand_tree->Hands(st, lbd), opp_probs,
			  sum_opp_probs, total_card_probs);
      return vals;
    }
  }
  if (st > last_st) {
    return StreetInitial(node, lbd, p, opp_probs, hand_tree, sumprobs,
			 root_bd_st, root_bd, buckets, card_abstraction);
  }
  if (node->PlayerActing() == p) {
    return OurChoice(node, lbd, p, opp_probs, sum_opp_probs,
		     total_card_probs, hand_tree, sumprobs, root_bd_st,
		     root_bd, buckets, card_abstraction);
  } else {
    return OppChoice(node, lbd, p, opp_probs, sum_opp_probs,
		     total_card_probs, hand_tree, sumprobs, root_bd_st,
		     root_bd, buckets, card_abstraction);
  }
}

// We use a board value of 0.  Recall that we have constructed a subgame
// that is specific to the current board.  So the local board index is 0.
double *DynamicCBR::Compute(Node *node, unsigned int p,
			    double *opp_probs, unsigned int gbd,
			    HandTree *hand_tree, const CFRValues *sumprobs,
			    unsigned int root_bd_st, unsigned int root_bd,
			    const Buckets &buckets,
			    const CardAbstraction &card_abstraction) {
  unsigned int st = node->Street();
  // time_t start_t = time(NULL);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  double sum_opp_probs;
  double *total_card_probs = new double[num_hole_card_pairs];
  unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  SetStreetBuckets(st, gbd, buckets, hands);
  CommonBetResponseCalcs(st, hands, opp_probs, &sum_opp_probs,
			 total_card_probs);
  double *vals = Process(node, lbd, p, opp_probs, sum_opp_probs,
			 total_card_probs, hand_tree, sumprobs, root_bd_st,
			 root_bd, buckets, card_abstraction, st);
  // Temporary?  Make our T values like T values constructed by build_cbrs,
  // by casting to float.
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    vals[i] = (float)vals[i];
  }
  delete [] total_card_probs;
#if 0
  // EVs for our hands are summed over all opponent hole card pairs.  To
  // compute properly normalized EV, need to divide by that number.
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
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
#endif

  FloorCVs(node, opp_probs, hands, vals);
  return vals;
}

// Pass in the player who you want CBR values for.
// Things get confusing in endgame solving.  Suppose we are doing endgame
// solving for P0.  We might say cfr_target_p is 0.  Then I want T-values for
// P1.  So I pass in 1 to Compute(). We'll need the reach probs of P1's
// opponent, who is P0.
double *DynamicCBR::Compute(Node *node, double **reach_probs,
			    unsigned int gbd, HandTree *hand_tree,
			    const CFRValues *sumprobs,
			    unsigned int root_bd_st, unsigned int root_bd,
			    const Buckets &buckets,
			    const CardAbstraction &card_abstraction,
			    unsigned int target_p, bool cfrs) {
  cfrs_ = cfrs;
  // For now assume that we always want to zero sum
  if (! cfrs_) {
    double *p0_cvs = Compute(node, 0, reach_probs[1], gbd, hand_tree,
			     sumprobs, root_bd_st, root_bd, buckets,
			     card_abstraction);
    double *p1_cvs = Compute(node, 1, reach_probs[0], gbd, hand_tree,
			     sumprobs, root_bd_st, root_bd, buckets,
			     card_abstraction);
    unsigned int st = node->Street();
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    // Don't pass in bd.  This is a local hand tree specific to the current
    // board.
    unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
    const CanonicalCards *hands = hand_tree->Hands(st, lbd);
    ZeroSumCVs(p0_cvs, p1_cvs, num_hole_card_pairs, reach_probs, hands);
    if (target_p == 1) {
      delete [] p0_cvs;
      return p1_cvs;
    } else {
      delete [] p1_cvs;
      return p0_cvs;
    }
  } else {
    return Compute(node, target_p, reach_probs[target_p^1], gbd,
		   hand_tree, sumprobs, root_bd_st, root_bd, buckets,
		   card_abstraction);
  }
}

