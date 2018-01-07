// BCBR values are "bucketed counterfactual best-response values".  These
// can be used in the CFR-D method of resolving subgames.  The code here
// generates CBR values for bucketed systems whereas cbr_thread.cpp handles
// base systems built without card abstraction.
//
// The best-response values computed here are within-abstraction best-response
// values rather than real-game best-response values.
//
// We handle imperfect recall card abstractions as well as perfect recall card
// abstractions.  However, in the case of imperfect recall, we do not produce
// true best-response values (it would be computationally impractical), but
// rather an approximation of those values.
//
// There are three types of tree traversal (known as "passes") that are
// performed.
//
// In the first pass, we compute the counterfactual value of every
// hand at every terminal node, and aggregates those values into bucket
// values.
//
// The second and third passes are executed on a single street at a time,
// and from the last street (e.g., the river) to the first street (the
// preflop).  The second pass identifies the best-response action in the
// abstract game; i.e., the best action for each *bucket* at each decision
// point.  The third pass computes counterfactual values for *hands* using the
// bucket-level best-response strategy computed in the second pass.  The
// third pass also stores the bucket values for the previous street at
// the street-initial node for the next street.  For example, turn bucket
// values at CC/CC/CC.
// 
// The BCBR values are written out in normal hand order on every street except
// the final street.  On the final street, the values are written out for hands
// as sorted by hand strength.
//
// The values for a hand depend only on the opponent's strategy above and below
// a given node.  P0's values are distinct from P1's values.
//
// We repeatedly execute the third pass on later street portions of the tree.
// We could avoid this if we are willing to cache card values at street-initial
// nodes.

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
#include "bcbr_thread.h"
#include "vcfr_state.h"
#include "vcfr.h"

using namespace std;

BCBRThread::BCBRThread(const CardAbstraction &ca, const BettingAbstraction &ba,
		       const CFRConfig &cc, const Buckets &buckets,
		       const BettingTree *betting_tree, unsigned int p,
		       HandTree *trunk_hand_tree, unsigned int thread_index,
		       unsigned int num_threads, unsigned int it,
		       BCBRThread **threads, bool trunk) :
  VCFR(ca, ba, cc, buckets, betting_tree, num_threads) {
  p_ = p;
  trunk_hand_tree_ = trunk_hand_tree;
  thread_index_ = thread_index;
  it_ = it;
  threads_ = threads;

  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    best_response_streets_[st] = true;
  }
  br_current_ = false;
  value_calculation_ = true;
  prune_ = false;

  // Always want sumprobs for the opponent.
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    players[p] = p != p_;
  }
  if (trunk) {
    // Should handle asymmetric systems
    // Should honor sumprobs_streets_
    sumprobs_.reset(new CFRValues(players.get(), true, nullptr,
				  betting_tree_, 0, 0, card_abstraction_,
				  buckets_.NumBuckets(), compressed_streets_));

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

    unsigned int num_terminals = betting_tree_->NumTerminals();
    terminal_bucket_vals_ = new double *[num_terminals];
    for (unsigned int i = 0; i < num_terminals; ++i) {
      // Don't know the street so can't allocate the right size array
      terminal_bucket_vals_[i] = nullptr;
    }
    si_bucket_vals_ = new double **[max_street + 1];
    si_bucket_vals_[0] = nullptr;
    for (unsigned int st = 1; st <= max_street; ++st) {
      // Assume all street-initial nodes (postflop) are P2 choice
      unsigned int num_nonterminals = betting_tree_->NumNonterminals(0, st);
      si_bucket_vals_[st] = new double *[num_nonterminals];
      for (unsigned int nt = 0; nt < num_nonterminals; ++nt) {
	// Don't know which nonterminals are street-initial at this point
	si_bucket_vals_[st][nt] = nullptr;
      }
    }

    best_succs_ = new unsigned char **[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      unsigned int num_buckets = buckets_.NumBuckets(st);
      unsigned int num_nonterminals = betting_tree_->NumNonterminals(p_, st);
      best_succs_[st] = new unsigned char *[num_nonterminals];
      for (unsigned int nt = 0; nt < num_nonterminals; ++nt) {
	best_succs_[st][nt] = new unsigned char[num_buckets];
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  best_succs_[st][nt][b] = 255;
	}
      }
    }
  } else {
    // We are not the trunk thread
    sumprobs_.reset(nullptr);
  }
}

BCBRThread::~BCBRThread(void) {
  // The inner arrays of terminal_bucket_vals_ and si_bucket_vals_ should
  // already have been deleted (and replaced by nullptr).  Doesn't hurt to
  // make sure here.
  unsigned int num_terminals = betting_tree_->NumTerminals();
  for (unsigned int i = 0; i < num_terminals; ++i) {
    delete [] terminal_bucket_vals_[i];
  }
  delete [] terminal_bucket_vals_;

  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 1; st <= max_street; ++st) {
    // Assume all street-initial nodes (postflop) are P2 choice
    unsigned int num_nonterminals = betting_tree_->NumNonterminals(0, st);
    for (unsigned int nt = 0; nt < num_nonterminals; ++nt) {
      delete [] si_bucket_vals_[st][nt];
    }
    delete [] si_bucket_vals_[st];
  }
  delete [] si_bucket_vals_;

  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int num_nonterminals = betting_tree_->NumNonterminals(p_, st);
    for (unsigned int nt = 0; nt < num_nonterminals; ++nt) {
      delete [] best_succs_[st][nt];
    }
    delete [] best_succs_[st];
  }
  delete [] best_succs_;
}

void BCBRThread::WriteValues(Node *node, unsigned int gbd, bool alt,
			     const string &action_sequence, double *vals) {
  char dir[500], buf[500];
  unsigned int street = node->Street();
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s/bcbrs.%u.p%u/%s",
	  Files::NewCFRBase(), Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str(), it_, p_,
	  action_sequence.c_str());
  Mkdir(dir);  
  sprintf(buf, "%s/%s.%u", dir, alt ? "alt_vals" : "vals", gbd);
  Writer writer(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(street);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    writer.WriteFloat((float)vals[i]);
  }
}

static unsigned int g_bcbr_limp_count = 0;
static unsigned int g_bcbr_nolimp_count = 0;

double *BCBRThread::OurChoice(Node *node, unsigned int lbd,
			      const VCFRState &state) {
  unsigned int st = node->Street();
  double *vals;
  
  if (first_pass_ || st < target_st_) {
    vals = VCFR::OurChoice(node, lbd, state);
  } else {
    unsigned int nt = node->NonterminalID();
    unsigned int num_succs = node->NumSuccs();
    double **succ_card_vals = new double *[num_succs];
    for (unsigned int s = 0; s < num_succs; ++s) {
      VCFRState succ_state(state, node, s);
      succ_card_vals[s] = Process(node->IthSucc(s), lbd, succ_state, st);
    }
    
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    vals = new double[num_hole_card_pairs];
    double *alt_vals = nullptr;
    if (st == target_st_) {
      alt_vals = new double[num_hole_card_pairs];
    }

    unsigned int **street_buckets = state.StreetBuckets();
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      unsigned int b = street_buckets[st][i];
      unsigned int s = best_succs_[st][nt][b];
      if (s == 255) {
	fprintf(stderr, "best_succs_[%u][%u][%u] unset\n", st, nt, b);
	exit(-1);
      }
      if (st == 0 && node->PlayerActing() == 1 && nt == 0) {
	if (s == 0) ++g_bcbr_limp_count;
	else        ++g_bcbr_nolimp_count;
      }
      vals[i] = succ_card_vals[s][i];
      if (st == target_st_) {
	double max_alt_val = succ_card_vals[0][i];
	for (unsigned int s = 1; s < num_succs; ++s) {
	  double v = succ_card_vals[s][i];
	  if (v > max_alt_val) max_alt_val = v;
	}
	alt_vals[i] = max_alt_val;
      }
    }

    for (unsigned int s = 0; s < num_succs; ++s) {
      delete [] succ_card_vals[s];
    }
    delete [] succ_card_vals;

    if (st == target_st_) {
      unsigned int gbd = 0;
      if (st > 0) {
	gbd = BoardTree::GlobalIndex(state.RootBdSt(), state.RootBd(), st,
				     lbd);
      }
      WriteValues(node, gbd, false, state.ActionSequence(), vals);
      WriteValues(node, gbd, true, state.ActionSequence(), alt_vals);
    }
  }
  
  return vals;
}

// Can't skip succ even if succ_sum_opp_probs is zero.  I need to write
// out BCBR values at every node.
double *BCBRThread::OppChoice(Node *node, unsigned int lbd, 
			      const VCFRState &state) {
  double *vals = VCFR::OppChoice(node, lbd, state);

  unsigned int st = node->Street();
  if (! first_pass_ && st == target_st_) {
    unsigned int gbd = 0;
    if (st > 0) {
      gbd = BoardTree::GlobalIndex(state.RootBdSt(), state.RootBd(), st, lbd);
    }
    WriteValues(node, gbd, false, state.ActionSequence(), vals);
  }

  return vals;
}

double *BCBRThread::Process(Node *node, unsigned int lbd, 
			    const VCFRState &state, unsigned int last_st) {
  unsigned int st = node->Street();
  double *vals = VCFR::Process(node, lbd, state, last_st);
  if (first_pass_) {
    if (node->Terminal()) {
      unsigned int tid = node->TerminalID();
      double *bucket_vals = terminal_bucket_vals_[tid];
      if (bucket_vals == nullptr) {
	unsigned int num_buckets = buckets_.NumBuckets(st);
	bucket_vals = new double[num_buckets];
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  bucket_vals[b] = 0.0;
	}
	terminal_bucket_vals_[tid] = bucket_vals;
      }
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int **street_buckets = state.StreetBuckets();
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	unsigned int b = street_buckets[st][i];
	bucket_vals[b] += vals[i];
      }
    }
  } else {
    // Third pass
    if (st == target_st_ && last_st == target_st_ - 1) {
      unsigned int pst = st - 1;
      unsigned int nt = node->NonterminalID();
      double *bucket_vals = si_bucket_vals_[st][nt];
      if (bucket_vals == nullptr) {
	unsigned int num_buckets = buckets_.NumBuckets(pst);
	bucket_vals = new double[num_buckets];
	for (unsigned int b = 0; b < num_buckets; ++b) {
	  bucket_vals[b] = 0.0;
	}
	si_bucket_vals_[st][nt] = bucket_vals;
      }
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(pst);
      unsigned int **street_buckets = state.StreetBuckets();
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	unsigned int b = street_buckets[pst][i];
	bucket_vals[b] += vals[i];
      }
    }
  }
  return vals;
}

double *BCBRThread::SecondPassOurChoice(Node *node) {
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  double **succ_bucket_vals = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    succ_bucket_vals[s] = SecondPass(node->IthSucc(s), st);
  }

  double *bucket_vals = nullptr;
  if (st == target_st_) {
    unsigned int num_buckets = buckets_.NumBuckets(st);
    bucket_vals = new double[num_buckets];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      double best_bv = succ_bucket_vals[0][b];
      unsigned int bs = 0;
      for (unsigned int s = 1; s < num_succs; ++s) {
	double succ_bv = succ_bucket_vals[s][b];
	if (succ_bv > best_bv) {
	  best_bv = succ_bv;
	  bs = s;
	}
      }
      best_succs_[st][nt][b] = bs;
      bucket_vals[b] = best_bv;
    }
  }

  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_bucket_vals[s];
  }
  delete [] succ_bucket_vals;

  return bucket_vals;
}

double *BCBRThread::SecondPassOppChoice(Node *node) {
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int num_buckets = buckets_.NumBuckets(st);

  double *bucket_vals = SecondPass(node->IthSucc(0), st);
  for (unsigned int s = 1; s < num_succs; ++s) {
    double *succ_bucket_vals = SecondPass(node->IthSucc(s), st);
    if (st == target_st_) {
      for (unsigned int b = 0; b < num_buckets; ++b) {
	bucket_vals[b] += succ_bucket_vals[b];
      }
      delete [] succ_bucket_vals;
    }
  }

  return bucket_vals;
}

double *BCBRThread::SecondPass(Node *node, unsigned int last_st) {
  unsigned int st = node->Street();
  if (node->Terminal()) {
    if (st == target_st_) {
      unsigned int tid = node->TerminalID();
      double *bucket_vals = terminal_bucket_vals_[tid];
      terminal_bucket_vals_[tid] = nullptr;
      return bucket_vals;
    } else {
      return nullptr;
    }
  } else if (st == target_st_ + 1) {
    unsigned int nt = node->NonterminalID();
    double *bucket_vals = si_bucket_vals_[st][nt];
    si_bucket_vals_[st][nt] = nullptr;
    return bucket_vals;
  } else {
    if (node->PlayerActing() == p_) {
      return SecondPassOurChoice(node);
    } else {
      return SecondPassOppChoice(node);
    }
  }
}

// Used for the first and third passes
void BCBRThread::CardPass(bool first_pass) {
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);

  first_pass_ = first_pass;
  double *opp_probs = AllocateOppProbs(true);
  unsigned int **street_buckets = AllocateStreetBuckets();
  VCFRState state(opp_probs, street_buckets, trunk_hand_tree_, p_, nullptr,
		  sumprobs_.get());
  SetStreetBuckets(0, 0, state);
  double *vals = Process(betting_tree_->Root(), 0, state, 0);
  DeleteStreetBuckets(street_buckets);
  delete [] opp_probs;

  if (! first_pass) {
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
  }

  delete [] vals;
}

void BCBRThread::Go(void) {
  time_t start_t = time(NULL);

  CardPass(true);
  int max_street = Game::MaxStreet();
  for (int st = max_street; st >= 0; --st) {
    target_st_ = st;
    double *bucket_vals = SecondPass(betting_tree_->Root(), 0);
    delete [] bucket_vals;
    CardPass(false);
  }

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  fprintf(stderr, "Processing took %.1f seconds\n", diff_sec);

  printf("Limp count %u/%u\n", g_bcbr_limp_count,
	 g_bcbr_limp_count + g_bcbr_nolimp_count);
}
