// We take a base strategy and expand it into an expanded strategy that
// has more bet sizes than the base strategy.  We use translation to
// determine the action probabilities for bets that are not in the base
// strategy.  Otherwise the base strategy is copied into the expanded
// strategy.
//
// To be more precise, we handle the target player and the opponent
// differently.  The target player is assumed to never use the new bets.
// He just gets the distribution over actions from the base.  The opponent
// is allowed to use the new bets, of course, so we must use translation to
// determine how to respond.
//
// In general, as we build the expanded strategy, we maintain a vector of base
// nodes and weights.  The weights sum to 1.0.  When the expanded node is
// within the base abstraction, the vector is length one, containing just the
// corresponding base node, and the weight is 1.0.  But let's say we get to a
// bet size in the expanded strategy that is not in the base strategy.  For
// example, a 3/4 pot bet and we only have 1/2 pot and 1x pot.  We then
// maintain a vector of the two nodes corresponding to the adjacent bet
// sizes (1/2 pot and 1x pot), with the weights given by the translation
// formula (e.g., Sam's formula).
//
// Note that we will encounter betting sequences in which multiple bets are
// outside of the base abstraction.  When we get to the second new bet,
// each base node in our vector is split a second time; so instead of a vector
// of two corresponding base nodes, we have a vector of four corresponding
// base nodes.
//
// Things get trickier when we have a new bet size that is smaller than any
// bet size in the base betting abstraction.  Effectively what we want to do
// is translate between a size zero bet and a bet of the next larger size in
// the betting abstraction.  But, to be conservative, I want to get the
// raise probabilities solely from the next larger bet action.  I accomplish
// this in a slightly roundabout fashion.  I get the translation weights in
// the usual way, interpolating between a bet size of zero and the next
// larger bet size.  I add two copies of the next larger bet node with these
// two weights.  One copy is marked "don't fold".  This copy corresponds to
// translating to a zero-size bet.  The other copy is not so marked; this copy
// corresponds to translating to the next larger bet.  On the subsequent
// response, we make sure not to fold to the "don't fold" node; the fold
// prob is added to the call prob.
//
// What happens if the expanded system is missing a bet from the base
// system?
//
// Have trouble with situations like the following:
//   Expanded tree has 2x pot bet, missing from base tree
//   We interpolate between 1x pot bet and all-in
//   We see another bet in the expanded tree
//   We cannot find a corresponding bet in the all-in node
//
// Expect to see problems with situations like the following:
//  Expanded tree has 2x pot bet
//  Base tree is too close too all-in; has no larger bet
//  Cannot interpolate
//  If a bet is all-in, use it as the larger bet
//
// Can't I end up with weighting problems?  For example:
// Expanded node has pot size 200.  We have a pot size bet of 200.
// Corresponding base nodes have pot sizes of 150 and 300.  For the larger
//   base node, a pot size bet is not possible.  So we only use the smaller
//   base node in determining whether to call the pot size bet.  That seems
//   wrong.
// What's the solution?  Use the closest approximation that we have?
//
// I calculate new_sumprobs even when num_succs is 1.  Doesn't hurt.  In
// SetValues() I do nothing.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"

using namespace std;

struct BaseNode {
  Node *node;
  unsigned int pot_size;
  bool dont_fold;
};

class Expander {
public:
  Expander(const CardAbstraction &ca, const BettingAbstraction &base_ba,
	   const BettingAbstraction &expanded_ba, const CFRConfig &base_cc,
	   const CFRConfig &expanded_cc, const Buckets &buckets,
	   unsigned int it, unsigned int p, bool nearest);
  ~Expander(void) {}

  void Go(void);
private:
  void Process(Node *node, unsigned int pot_size, vector<BaseNode> *base_nodes,
	       vector<double> *translation_probs);

  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const BettingAbstraction &expanded_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
  const CFRConfig &expanded_cfr_config_;
  const Buckets &buckets_;
  unsigned int it_;
  unsigned int p_;
  bool nearest_;
  unique_ptr<BettingTree> base_betting_tree_;
  unique_ptr<BettingTree> expanded_betting_tree_;
  unique_ptr<CFRValues> base_sumprobs_;
  unique_ptr<CFRValues> expanded_sumprobs_;
};

Expander::Expander(const CardAbstraction &ca,
		   const BettingAbstraction &base_ba,
		   const BettingAbstraction &expanded_ba,
		   const CFRConfig &base_cc, const CFRConfig &expanded_cc,
		   const Buckets &buckets, unsigned int it, unsigned int p,
		   bool nearest) :
  card_abstraction_(ca), base_betting_abstraction_(base_ba),
  expanded_betting_abstraction_(expanded_ba), base_cfr_config_(base_cc),
  expanded_cfr_config_(expanded_cc), buckets_(buckets) {
  it_ = it;
  p_ = p;
  nearest_ = nearest;
  if (base_betting_abstraction_.Asymmetric()) {
    base_betting_tree_.reset(
	       BettingTree::BuildAsymmetricTree(base_betting_abstraction_, p));
  } else {
    base_betting_tree_.reset(BettingTree::BuildTree(base_betting_abstraction_));
  }
  if (expanded_betting_abstraction_.Asymmetric()) {
    expanded_betting_tree_.reset(
	   BettingTree::BuildAsymmetricTree(expanded_betting_abstraction_, p));
  } else {
    expanded_betting_tree_.reset(
		     BettingTree::BuildTree(expanded_betting_abstraction_));
  }

  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    players[p] = (p_ == p);
  }
  bool *base_compressed_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    base_compressed_streets[st] = false;
  }
  const vector<unsigned int> &bcsv = base_cfr_config_.CompressedStreets();
  unsigned int num_bcsv = bcsv.size();
  for (unsigned int i = 0; i < num_bcsv; ++i) {
    unsigned int st = bcsv[i];
    base_compressed_streets[st] = true;
  }

  bool *expanded_compressed_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    expanded_compressed_streets[st] = false;
  }
  const vector<unsigned int> &ecsv = expanded_cfr_config_.CompressedStreets();
  unsigned int num_ecsv = ecsv.size();
  for (unsigned int i = 0; i < num_ecsv; ++i) {
    unsigned int st = ecsv[i];
    expanded_compressed_streets[st] = true;
  }

  base_sumprobs_.reset(new CFRValues(players.get(), true, nullptr,
				     base_betting_tree_.get(), 0, 0,
				     ca, buckets_, base_compressed_streets));
  expanded_sumprobs_.reset(new CFRValues(players.get(), true, nullptr,
					 expanded_betting_tree_.get(), 0, 0,
					 ca, buckets_,
					 expanded_compressed_streets));

  delete [] base_compressed_streets;
  delete [] expanded_compressed_streets;
  
  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(),
	  base_cfr_config_.CFRConfigName().c_str());
  if (base_betting_abstraction_.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", p_);
    strcat(dir, buf);
  }
  base_sumprobs_->Read(dir, it_, base_betting_tree_->Root(), "x", kMaxUInt);

  expanded_sumprobs_->AllocateAndClearDoubles(expanded_betting_tree_->Root(),
					      kMaxUInt);
}

// fracs are bet sizes expressed as fractions of the pot.
static double BelowProb(double actual_frac, double below_frac,
			double above_frac) {
  double below_prob =
      ((above_frac - actual_frac) * (1.0 + below_frac)) /
      ((above_frac - below_frac) * (1.0 + actual_frac));
  return below_prob;
}

// We walk the expanded tree maintaining a vector of the base nodes that
// are considered "analogous" to the current expanded tree node.  We also
// maintain the translation probabilities for each analogous base node.
//
// pot_size includes any pending bets; it is not identical to node->PotSize()
void Expander::Process(Node *node, unsigned int pot_size,
		       vector<BaseNode> *base_nodes,
		       vector<double> *translation_probs) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int this_p = node->PlayerActing();
  if (this_p == p_) {
    unsigned int st = node->Street();
    unsigned int num_holdings;
    if (buckets_.None(st)) {
      unsigned int num_boards = BoardTree::NumBoards(st);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      num_holdings = num_boards * num_hole_card_pairs;
    } else {
      num_holdings = buckets_.NumBuckets(st);
    }
    unsigned int num_base = base_nodes->size();
    unsigned int num_actions = num_holdings * num_succs;
    double *new_sumprobs = new double[num_actions];
    // We initialize all sumprobs to zero.  This is going to mean that
    // the "new" action will get a sumprob of zero, because it never gets
    // set below.
    for (unsigned int a = 0; a < num_actions; ++a) new_sumprobs[a] = 0;

    // In general, every base node bet succ should map into an expanded node
    // succ.  Compute that mapping here, and the reverse mapping from
    // expanded succs to base succs.  This is often 1:1, but will not
    // always be, so we allow for multiple base succs mapping to the same
    // expanded succ.
    vector<unsigned int> **expanded_to_base =
      new vector<unsigned int> *[num_base];
    for (unsigned int i = 0; i < num_base; ++i) {
      BaseNode bn = (*base_nodes)[i];
      Node *base_node = bn.node;
      if (base_node->Terminal()) {
	expanded_to_base[i] = nullptr;
	continue;
      }
      unsigned int base_pot_size = bn.pot_size;
      unsigned int num_base_succs = base_node->NumSuccs();
      expanded_to_base[i] = new vector<unsigned int>[num_succs];
      for (unsigned int bs = 0; bs < num_base_succs; ++bs) {
	if (bs == base_node->CallSuccIndex()) {
	  // base_to_expanded[i][bs] = node->CallSuccIndex();
	  continue;
	}
	if (bs == base_node->FoldSuccIndex()) {
	  // base_to_expanded[i][bs] = node->FoldSuccIndex();
	  continue;
	}
	Node *base_succ = base_node->IthSucc(bs);
	Node *base_call = base_succ->IthSucc(base_succ->CallSuccIndex());
	bool base_bet_all_in = (base_call->PotSize() ==
				2 * base_betting_abstraction_.StackSize());
	unsigned int base_bet_size =
	  (base_call->PotSize() - base_pot_size) / 2;
	bool base_min_bet =
	  (base_bet_size == base_betting_abstraction_.MinBet());
	double base_bet_frac = base_bet_size / (double)base_pot_size;
	double min_dist = 999.9;
	unsigned int best_es = kMaxUInt;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == node->CallSuccIndex() || s == node->FoldSuccIndex()) {
	    continue;
	  }
	  Node *exp_succ = node->IthSucc(s);
	  Node *exp_call = exp_succ->IthSucc(exp_succ->CallSuccIndex());
	  unsigned int exp_call_pot_size = exp_call->PotSize();
	  bool exp_bet_all_in =
	    (exp_call_pot_size ==
	     2 * expanded_betting_abstraction_.StackSize());
	  double dist = 999.9;
	  if (base_bet_all_in && exp_bet_all_in) {
	    dist = 0;
	  } else {
	    // Note: use pot_size, not node->PotSize()
	    unsigned int exp_bet_size = (exp_call_pot_size - pot_size) / 2;
	    bool exp_min_bet =
	      (exp_bet_size == expanded_betting_abstraction_.MinBet());
	    if (base_min_bet && exp_min_bet) {
	      dist = 0;
	    } else {
	      double exp_bet_frac = exp_bet_size / (double)pot_size;
	      dist = fabs(base_bet_frac - exp_bet_frac);
	    }
	  }
	  if (dist < min_dist) {
	    min_dist = dist;
	    best_es = s;
	  }
	}
#if 0
	// If the base node has a smaller pot size, then the best match
	// for a 0.7x bet in the expanded system may be much less than
	// 0.7x.  Because it only takes, e.g., a 1/2 pot bet in the expanded
	// system to get all-in.
	// A second problem.  In the same scenario, a base 0.7x bet and
	// a base all-in may map to the same expanded succ.
	// We used to force the expanded all-in to acquire only from the
	// base all-in.  That wasn't good.
	// Likewise, a base 0.7x bet and a base 1.0x bet might also map
	// to the same expanded all-in succ.
	if (min_dist > 0.05) {
	  fprintf(stderr, "Found no expanded succ that matched base succ\n");
	  fprintf(stderr, "min_dist %f\n", min_dist);
	  fprintf(stderr, "base_bet_frac %f base_bet_size %u "
		  "base_pot_size %u\n", base_bet_frac, base_bet_size,
		  base_pot_size);
	  exit(-1);
	}
#endif
	if (best_es == kMaxUInt) {
	  // This can happen.  Saw it when expanded system is facing an
	  // all-in bet.
	  if (pot_size != 2 * expanded_betting_abstraction_.StackSize()) {
	    fprintf(stderr, "Base succ maps to no expanded succ\n");
	    fprintf(stderr, "Num succs %u ps %u nps %u\n", num_succs,
		    pot_size, node->PotSize());
	    exit(-1);
	  } else {
	    // Map base raise to call
	    unsigned int cs = node->CallSuccIndex();
	    expanded_to_base[i][cs].push_back(bs);
	  }
	} else {
	  // Should only have multiple base succs mapping to the same expanded
	  // succ if the expanded succ is all-in.  Could check that here.
	  expanded_to_base[i][best_es].push_back(bs);
	}
      }
    }
    
    double **base_probs = new double *[num_base];
    for (unsigned int i = 0; i < num_base; ++i) {
      BaseNode bn = (*base_nodes)[i];
      Node *base_node = bn.node;
      if (base_node->Terminal()) {
	// A river all-in node
	base_probs[i] = nullptr;
	continue;
      }
      unsigned int base_nt = base_node->NonterminalID();
      unsigned int num_base_succs = base_node->NumSuccs();
      unsigned int num_actions = num_holdings * num_base_succs;
      base_probs[i] = new double[num_actions];
      for (unsigned int a = 0; a < num_actions; ++a) {
	base_probs[i][a] = 0;
      }
      if (num_base_succs > 1) {
	unsigned int default_succ_index = base_node->DefaultSuccIndex();
	if (base_sumprobs_->Ints(p_, st)) {
	  int *i_base_sumprobs = nullptr;
	  base_sumprobs_->Values(p_, st, base_nt, &i_base_sumprobs);
	  for (unsigned int j = 0; j < num_holdings; ++j) {
	    RegretsToProbs(i_base_sumprobs + j * num_base_succs, num_base_succs,
			   true, false, default_succ_index, 0, 0, nullptr,
			   &base_probs[i][j * num_base_succs]);
	  }
	} else {
	  double *d_base_sumprobs = nullptr;
	  base_sumprobs_->Values(p_, st, base_nt, &d_base_sumprobs);
	  for (unsigned int j = 0; j < num_holdings; ++j) {
	    RegretsToProbs(d_base_sumprobs + j * num_base_succs, num_base_succs,
			   true, false, default_succ_index, 0, 0, nullptr,
			   &base_probs[i][j * num_base_succs]);
	  }
	}
      }
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      Node *succ = node->IthSucc(s);
      unsigned int new_pot_size;
      vector<BaseNode> new_base_nodes;
      vector<double> new_translation_probs;
      if (s == node->CallSuccIndex() || s == node->FoldSuccIndex()) {
	new_pot_size = succ->PotSize();
	for (unsigned int i = 0; i < num_base; ++i) {
	  BaseNode bn = (*base_nodes)[i];
	  Node *base_node = bn.node;
#if 0
	  // This is possible
	  if (base_node->Terminal()) {
	    fprintf(stderr, "Base node terminal our choice\n");
	    fprintf(stderr, "nps %u ps %u bps %u\n", node->PotSize(), pot_size,
		    bn.pot_size);
	  }
#endif
	  bool dont_fold = bn.dont_fold;
	  bool base_all_in = (base_node->PotSize() ==
			      2 * base_betting_abstraction_.StackSize());
	  double translation_prob = (*translation_probs)[i];
	  // If the base node is all-in, there are two possibilities:
	  // 1) We are facing a bet.  Contribute a distribution of
	  //    "always call", weighted by the translation prob.
	  // 2) We are not facing a bet.  Just skip this base node.  If
	  //    there are other base nodes that are not all-in, they will
	  //    dictate the distribution.
	  if (base_all_in) {
	    bool facing_bet = node->HasFoldSucc();
	    if (s == node->CallSuccIndex() && facing_bet) {
	      for (unsigned int h = 0; h < num_holdings; ++h) {
		unsigned int ea = h * num_succs + s;
		new_sumprobs[ea] += translation_prob;
	      }
	      BaseNode nbn;
	      nbn.dont_fold = false;
	      nbn.node = base_node;
	      nbn.pot_size = base_node->PotSize();
	      new_base_nodes.push_back(nbn);
	      new_translation_probs.push_back(translation_prob);
	    }
	    continue;
	  }
	  unsigned int num_base_succs = base_node->NumSuccs();
	  unsigned int bs;
	  if (s == node->CallSuccIndex()) {
	    bs = base_node->CallSuccIndex();
	  } else {
	    bs = base_node->FoldSuccIndex();
	  }
	  BaseNode nbn;
	  nbn.dont_fold = false;
	  nbn.node = base_node->IthSucc(bs);
	  nbn.pot_size = base_node->IthSucc(bs)->PotSize();
	  new_base_nodes.push_back(nbn);
	  new_translation_probs.push_back(translation_prob);
	  for (unsigned int h = 0; h < num_holdings; ++h) {
	    unsigned int ea = h * num_succs + s;
	    unsigned int ba = h * num_base_succs + bs;
	    if (dont_fold && s == node->FoldSuccIndex()) {
	      // Add nothing to new_sumprobs[ea] - we are not allowed to
	      // fold.  new_sumprobs are initialized to zero
	    } else {
	      new_sumprobs[ea] += base_probs[i][ba] * translation_prob;
	      if (dont_fold && s == node->CallSuccIndex()) {
		if (base_node->FoldSuccIndex() != 1) {
		  fprintf(stderr, "Unexpected fold succ index\n");
		  exit(-1);
		}
		// Also want to add the base fold probs to the expanded
		// call probs.
		unsigned int bfs = base_node->FoldSuccIndex();
		unsigned int ba = h * num_base_succs + bfs;
		new_sumprobs[ea] += base_probs[i][ba] * translation_prob;
	      }
	    }
	  }
	}
      } else {
	// succ is a bet succ
	Node *call = succ->IthSucc(succ->CallSuccIndex());
	new_pot_size = call->PotSize();
#if 0
	bool all_in =
	  (call->PotSize() == 2 * expanded_betting_abstraction_.StackSize());
#endif
	// Note: use pot_size, not node->PotSize()
	// unsigned int bet_size = (call->PotSize() - pot_size) / 2;
	// bool min_bet = (bet_size == expanded_betting_abstraction_.MinBet());
	// double bet_frac = bet_size / (double)pot_size;
	for (unsigned int i = 0; i < num_base; ++i) {
	  BaseNode bn = (*base_nodes)[i];
	  Node *base_node = bn.node;
	  bool base_all_in = (base_node->PotSize() ==
			      2 * base_betting_abstraction_.StackSize());
	  // If this base node is all in, we want to add zero to this bet
	  // succ.  That requries no action, so we just continue here.
	  if (base_all_in) continue;
	  double translation_prob = (*translation_probs)[i];
	  unsigned int num_base_succs = base_node->NumSuccs();
#if 0
	  unsigned int base_pot_size = bn.pot_size;
	  unsigned int bs;
	  for (bs = 0; bs < num_base_succs; ++bs) {
	    if (bs == base_node->CallSuccIndex() ||
		bs == base_node->FoldSuccIndex()) {
	      continue;
	    }
	    Node *base_succ = base_node->IthSucc(bs);
	    Node *base_call = base_succ->IthSucc(base_succ->CallSuccIndex());
	    bool base_now_all_in = (base_call->PotSize() ==
				2 * base_betting_abstraction_.StackSize());
	    if (all_in && base_now_all_in) break;
	    // Note: use base_pot_size, not base_node->PotSize()
	    unsigned int base_bet_size =
	      (base_call->PotSize() - base_pot_size) / 2;
	    bool base_min_bet =
	      (base_bet_size == base_betting_abstraction_.MinBet());
	    if (min_bet && base_min_bet) break;
	    double base_bet_frac = base_bet_size / (double)base_pot_size;
	    if (fabs(base_bet_frac - bet_frac) < 0.001) break;
	  }
	  if (bs == num_base_succs) {
	  }
#endif
	  // If the vector is empty, this is a new action to be taken by us in
	  // the expanded system.  We need do nothing.  Sumprobs for this
	  // expanded action were initialized to zero.
	  const vector<unsigned int> &v = expanded_to_base[i][s];
	  unsigned int vnum = v.size();
	  for (unsigned int j = 0; j < vnum; ++j) {
	    unsigned int bs = v[j];
	    // Bet in base system matches bet in expanded system
	    BaseNode nbn;
	    nbn.dont_fold = false;
	    Node *base_succ = base_node->IthSucc(bs);
	    nbn.node = base_succ;
	    nbn.pot_size =
	      base_succ->IthSucc(base_succ->CallSuccIndex())->PotSize();
	    new_base_nodes.push_back(nbn);
	    new_translation_probs.push_back(translation_prob);
	    for (unsigned int h = 0; h < num_holdings; ++h) {
	      unsigned int ea = h * num_succs + s;
	      unsigned int ba = h * num_base_succs + bs;
	      new_sumprobs[ea] += base_probs[i][ba] * translation_prob;
	    }
	  }
	}
      }

      // If this is a new action for us, we don't need to recurse.  All
      // sumprobs below this point were intialized to zero which is fine.
      if (new_base_nodes.size() > 0) {
#if 0
	// If base node gets all-in, we stop splitting.  So this test
	// doesn't work.
	if (new_base_nodes.size() != base_nodes->size()) {
	  fprintf(stderr, "Expected sizes to match: %i %i\n",
		  (int)new_base_nodes.size(), (int)base_nodes->size());
	  exit(-1);
	}
#endif
	Process(node->IthSucc(s), new_pot_size, &new_base_nodes,
		&new_translation_probs);
      }
    }
    for (unsigned int i = 0; i < num_base; ++i) {
      delete [] expanded_to_base[i];
    }
    delete [] expanded_to_base;
    for (unsigned int i = 0; i < num_base; ++i) {
      delete [] base_probs[i];
    }
    delete [] base_probs;
    expanded_sumprobs_->SetValues(node, new_sumprobs);
    delete [] new_sumprobs;
  } else {
    for (unsigned int s = 0; s < num_succs; ++s) {
      bool call = (s == node->CallSuccIndex());
      bool fold = (s == node->FoldSuccIndex());
      Node *succ = node->IthSucc(s);
      bool all_in = false;
      bool min_bet = false;
      double bet_frac = 0;
      unsigned int new_pot_size;
      if (! call && ! fold) {
	Node *bet_call = succ->IthSucc(succ->CallSuccIndex());
	new_pot_size = bet_call->PotSize();
	// Use pot_size, not node->PotSize()
	unsigned int bet_size = (bet_call->PotSize() - pot_size) / 2;
	bet_frac = bet_size / (double)pot_size;
	if (bet_call->PotSize() ==
	    2 * expanded_betting_abstraction_.StackSize()) {
	  all_in = true;
	}
	if (bet_size == expanded_betting_abstraction_.MinBet()) {
	  min_bet = true;
	}
      } else {
	new_pot_size = succ->PotSize();
      }
      vector<BaseNode> new_base_nodes;
      vector<double> new_translation_probs;
      unsigned int num_base = base_nodes->size();
      for (unsigned int i = 0; i < num_base; ++i) {
	BaseNode bn = (*base_nodes)[i];
	Node *base_node = bn.node;
	// Is it possible to get here?
	if (base_node->Terminal()) {
	  fprintf(stderr, "Terminal base node in opp-choice\n");
	  exit(-1);
	}
	unsigned int base_pot_size = bn.pot_size;
	double translation_prob = (*translation_probs)[i];
	unsigned int last_below = base_node->CallSuccIndex();
	unsigned int first_above = kMaxUInt;
	double below_frac = 0, above_frac = 0;
	unsigned int num_base_succs = base_node->NumSuccs();
	unsigned bs;
	if (call) {
	  bs = base_node->CallSuccIndex();
	} else if (fold) {
	  bs = base_node->FoldSuccIndex();
	} else {
	  for (bs = 0; bs < num_base_succs; ++bs) {
	    if (bs == base_node->CallSuccIndex() ||
		bs == base_node->FoldSuccIndex()) {
	      continue;
	    }
	    Node *base_bet = base_node->IthSucc(bs);
	    Node *base_bet_call = base_bet->IthSucc(base_bet->CallSuccIndex());

	    bool base_all_in = (base_bet_call->PotSize() ==
				2 * base_betting_abstraction_.StackSize());
	    if (all_in && base_all_in) {
	      break;
	    }

	    // Use base_pot_size, not base_node->PotSize()
	    unsigned int bet_size =
	      (base_bet_call->PotSize() - base_pot_size) / 2;
	    double base_bet_frac = bet_size / (double)base_pot_size;
	    if (fabs(base_bet_frac - bet_frac) < 0.001) break;
	    
	    bool base_min_bet =
	      (bet_size == base_betting_abstraction_.MinBet());
	    if (min_bet && base_min_bet) break;
	    if (base_bet_frac < bet_frac) {
	      // This keeps getting reset so we know that below_frac
	      // corresponds to the last (i.e., largest) bet that is smaller
	      // than the actual bet.
	      last_below = bs;
	      below_frac = base_bet_frac;
	    }
	    if (base_bet_frac > bet_frac && first_above == kMaxUInt) {
	      // We only set this once so we know that above_frac corresponds
	      // to the first bet (if any) that is larger than the actual bet.
	      first_above = bs;
	      above_frac = base_bet_frac;
	    }
	  }
	}
	if (bs == num_base_succs && first_above == kMaxUInt) {
	  // This can happen when, for example, the expanded bet is pot size,
	  // but an 80% bet gets us all-in in this base node.  In this case,
	  // we want to use the all-in bet as the single translation.
	  bs = last_below;
	}
	if (bs == num_base_succs) {
	  // No matching bet in base system
	  // Interpolate between last_below and first_above
	  if (base_node->PotSize() ==
	      2 * base_betting_abstraction_.StackSize()) {
	    // If we're all-in in base abstraction, just leave current node in?
	    // Should I be following a call succ?
	    BaseNode nbn;
	    nbn.dont_fold = false;
	    nbn.node = base_node;
	    nbn.pot_size = base_node->PotSize();
	    new_base_nodes.push_back(nbn);
	    new_translation_probs.push_back(translation_prob);
	  } else {
	    if (first_above == kMaxUInt) {
	      fprintf(stderr, "Need to interpolate but no larger bet in "
		      "base abstraction: exp ps %u bf %f base ps %u "
		      "base ss %u ai %i\n",
		      pot_size, bet_frac, base_pot_size,
		      base_betting_abstraction_.StackSize(),
		      (int)all_in);
	      fprintf(stderr, "ent %u bnt %u s %u\n",
		      node->NonterminalID(), base_node->NonterminalID(), s);
	      exit(-1);
#if 0
	    } else if (last_below == base_node->CallSuccIndex()) {
	      // Translate to smallest bet for now
	      for (bs = 0; bs < num_base_succs; ++bs) {
		if (bs != base_node->CallSuccIndex() &&
		    bs != base_node->FoldSuccIndex()) {
		  break;
		}
	      }
	      if (bs == num_base_succs) {
		fprintf(stderr, "Couldn't find smallest bet succ?!?\n");
		exit(-1);
	      }
	      BaseNode nbn;
	      nbn.dont_fold = false;
	      nbn.node = base_node->IthSucc(bs);
	      new_base_nodes.push_back(nbn);
	      new_translation_probs.push_back(translation_prob);
#endif
	    } else if (nearest_) {
	      double below_prob = BelowProb(bet_frac, below_frac, above_frac);
	      BaseNode nbn;
	      unsigned int nbs;
	      if (below_prob >= 0.5) nbs = last_below;
	      else                   nbs = first_above;
	      Node *succ = base_node->IthSucc(nbs);
	      if (nbs == base_node->CallSuccIndex()) {
		nbn.dont_fold = true;
		nbn.pot_size = succ->PotSize();
		// We use the above node going forward, but mark that we
		// shouldn't fold at the next opportunity.
		nbn.node = base_node->IthSucc(first_above);
	      } else {
		nbn.dont_fold = false;
		nbn.pot_size = succ->IthSucc(succ->CallSuccIndex())->PotSize();
		nbn.node = succ;
	      }
	      new_base_nodes.push_back(nbn);
	      new_translation_probs.push_back(translation_prob);
	    } else {
	      double below_prob = BelowProb(bet_frac, below_frac, above_frac);
	      // Could check for below_prob between 0 and 1.  I think I've
	      // seen cases where it falls outside of this range.  Perhaps
	      // when below_frac == above_frac.  Or when one of them is
	      // zero or unset?
	      Node *first_above_succ = base_node->IthSucc(first_above);
	      Node *last_below_succ = base_node->IthSucc(last_below);
	      BaseNode nbn1, nbn2;
	      if (last_below == base_node->CallSuccIndex()) {
		nbn1.dont_fold = true;
		// We use the above node going forward, but mark that we
		// shouldn't fold at the next opportunity.
		nbn1.node = first_above_succ;
		// What should the pot size be?  Hmm.  Maybe use the pot
		// size from the above node?
		nbn1.pot_size = first_above_succ->IthSucc(
				first_above_succ->CallSuccIndex())->PotSize();
	      } else {
		nbn1.dont_fold = false;
		nbn1.node = last_below_succ;
		nbn1.pot_size = last_below_succ->IthSucc(
				  last_below_succ->CallSuccIndex())->PotSize();
	      }
	      new_base_nodes.push_back(nbn1);
	      new_translation_probs.push_back(translation_prob * below_prob);
	      nbn2.dont_fold = false;
	      nbn2.node = first_above_succ;
	      if (first_above == base_node->CallSuccIndex()) {
		nbn2.pot_size = first_above_succ->PotSize();
	      } else {
		nbn2.pot_size = first_above_succ->IthSucc(
			       first_above_succ->CallSuccIndex())->PotSize();
	      }
	      new_base_nodes.push_back(nbn2);
	      new_translation_probs.push_back(
				  translation_prob * (1.0 - below_prob));
	    }
	  }
	} else {
	  BaseNode nbn;
	  nbn.dont_fold = false;
	  nbn.node = base_node->IthSucc(bs);
	  if (bs == base_node->CallSuccIndex() ||
	      bs == base_node->FoldSuccIndex()) {
	    nbn.pot_size = nbn.node->PotSize();
	  } else {
	    nbn.pot_size =
	      nbn.node->IthSucc(nbn.node->CallSuccIndex())->PotSize();
	  }
	  new_base_nodes.push_back(nbn);
	  new_translation_probs.push_back(translation_prob);
	}
      }
      Process(node->IthSucc(s), new_pot_size, &new_base_nodes,
	      &new_translation_probs);
    }
  }
}

void Expander::Go(void) {
  vector<BaseNode> base_nodes;
  vector<double> translation_probs;
  BaseNode bn;
  bn.dont_fold = false;
  Node *base_root = base_betting_tree_->Root();
  unsigned int base_root_pot_size =
    base_root->IthSucc(base_root->CallSuccIndex())->PotSize();
  bn.node = base_root;
  bn.pot_size = base_root_pot_size;
  base_nodes.push_back(bn);
  translation_probs.push_back(1.0);
  Node *root = expanded_betting_tree_->Root();
  unsigned int root_pot_size = root->IthSucc(root->CallSuccIndex())->PotSize();
  Process(root, root_pot_size, &base_nodes, &translation_probs);
  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  expanded_betting_abstraction_.BettingAbstractionName().c_str(),
	  expanded_cfr_config_.CFRConfigName().c_str());
  if (expanded_betting_abstraction_.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", p_);
    strcat(dir, buf);
  }
  Mkdir(dir);
  expanded_sumprobs_->Write(dir, it_, expanded_betting_tree_->Root(), "x", p_);
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <base betting params> "
	  "<expanded betting params> <base CFR params> <expanded CFR params> "
	  "<it> [nearest|blend]\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 9) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_params = CreateCardAbstractionParams();
  card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    card_abstraction(new CardAbstraction(*card_params));
  unique_ptr<Params> base_betting_params = CreateBettingAbstractionParams();
  base_betting_params->ReadFromFile(argv[3]);
  unique_ptr<BettingAbstraction>
    base_betting_abstraction(new BettingAbstraction(*base_betting_params));
  unique_ptr<Params> expanded_betting_params = CreateBettingAbstractionParams();
  expanded_betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    expanded_betting_abstraction(
		    new BettingAbstraction(*expanded_betting_params));
  unique_ptr<Params> base_cfr_params = CreateCFRParams();
  base_cfr_params->ReadFromFile(argv[5]);
  unique_ptr<CFRConfig>
    base_cfr_config(new CFRConfig(*base_cfr_params));
  unique_ptr<Params> expanded_cfr_params = CreateCFRParams();
  expanded_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig>
    expanded_cfr_config(new CFRConfig(*expanded_cfr_params));
  unsigned int it;
  if (sscanf(argv[7], "%u", &it) != 1)       Usage(argv[0]);
  bool nearest = false;
  string m = argv[8];
  if (m == "nearest")    nearest = true;
  else if (m == "blend") nearest = false;
  else                   Usage(argv[0]);

  BoardTree::Create();
  Buckets buckets(*card_abstraction, false);
  
  for (unsigned int p = 0; p <= 1; ++p) {
    Expander expander(*card_abstraction, *base_betting_abstraction,
		      *expanded_betting_abstraction, *base_cfr_config,
		      *expanded_cfr_config, buckets, it, p, nearest);
    expander.Go();
  }
}
