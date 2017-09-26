// Code assumes all streets target_st_ or greater are bucketed.
//
// This code produces an estimate of the real-game best-response value.
// It is not a great estimate in the sense of corresponding closely to the
// full real-game best-response value.
//
// Would like to produce an estimate of the best-response value.
// Will probably need to sample from multiple target nodes because the
// exploitability is proportional to the pot size.
// We could explore every target node and board and sample with some
// probability.
//
// Is there a way of allowing the best-responder to vary his preflop
// strategy without "cheating"?  We could let him vary his preflop strategy
// and then play according to both players fixed postflop strategies.  But I
// don't think this would give us much.  We could let P0 choose to never fold.
// Or maybe to choose a fixed action for all preflop betting states that he
// chooses regardless of the cards he holds.  These would all allow a tiny
// amount of cheating.
//
// Why do I read the sumprobs multiple times?
//
// With a bucketed system, we will have to read all the sumprobs, no?

#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "dynamic_cbr.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"
#include "rand.h"
#include "split.h"

using namespace std;

class ApproxRGBR {
public:
  ApproxRGBR(const CardAbstraction &ca, const BettingAbstraction &ba,
	     const CFRConfig &cc, unsigned int it, double sample_prob,
	     bool always_call);
  ~ApproxRGBR(void);
  void Go(void);
private:
  double GetBRVal(Node *node, double **reach_probs, unsigned int bd,
		  unsigned int p, CFRValues *sumprobs, HandTree *hand_tree,
		  double *br_norm);
  CFRValues *GetSumprobs(void);
  void SampledCompute(Node *node, double **reach_probs, double *total_br_vals,
		      double *total_br_norms);
  void AlwaysCallSampledCompute(Node *node, double **reach_probs,
				double *total_br_vals, double *total_br_norms);
  double ***GetSuccReachProbs(Node *node, unsigned int bd,
			      double **reach_probs);
  void Terminal(Node *node, double **reach_probs, double *total_br_vals,
		double *total_br_norms);
  void Walk(Node *node, unsigned int bd, double **reach_probs,
	    double *total_br_vals, double *total_br_norms);
  
  unsigned int target_st_;
  double sample_prob_;
  bool always_call_;
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  Buckets buckets_;
  unsigned int it_;
  unique_ptr<BettingTree> betting_tree_;
  unique_ptr<DynamicCBR> dynamic_cbr_;
  unique_ptr<HandTree> trunk_hand_tree_;
  unique_ptr<CFRValues> trunk_sumprobs_;
  CFRValues *subgame_sumprobs_;
  unique_ptr<unsigned int []> board_samples_;
  unsigned int terminal_scaling_;
};

#if 0
static unsigned int NumTargetNodes(Node *node, unsigned int target_st) {
  if (node->Terminal())            return 0;
  if (node->Street() == target_st) return 1;
  unsigned int num_succs = node->NumSuccs();
  unsigned int num = 0;
  for (unsigned int s = 0; s < num_succs; ++s) {
    num += NumTargetNodes(node->IthSucc(s), target_st);
  }
  return num;
}
#endif

ApproxRGBR::ApproxRGBR(const CardAbstraction &ca, const BettingAbstraction &ba,
		       const CFRConfig &cc, unsigned int it,
		       double sample_prob, bool always_call) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(ca, false) {
  it_ = it;
  target_st_ = 1; // Hard-coded for now
  sample_prob_ = sample_prob;
  always_call_ = false;

  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = target_st_; st <= max_street; ++st) {
    if (buckets_.None(st)) {
      fprintf(stderr, "Expect all streets >= target_st to be bucketed\n");
      exit(-1);
    }
  }
  
  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  } else {
    betting_tree_.reset(BettingTree::BuildTree(betting_abstraction_));
  }
  
  // We need probs for both players
  unique_ptr<bool []> trunk_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    trunk_streets[st] = st < target_st_;
  }
  unique_ptr<bool []> compressed_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    compressed_streets[st] = false;
  }
  const vector<unsigned int> &csv = cfr_config_.CompressedStreets();
  unsigned int num_csv = csv.size();
  for (unsigned int i = 0; i < num_csv; ++i) {
    unsigned int st = csv[i];
    compressed_streets[st] = true;
  }
  trunk_sumprobs_.reset(new CFRValues(nullptr, true, trunk_streets.get(),
				      betting_tree_.get(), 0, 0,
				      card_abstraction_, buckets_,
				      compressed_streets.get()));
  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  }
  trunk_sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
  if (target_st_ == 0) {
    trunk_hand_tree_.reset(new HandTree(0, 0, 0));
  } else {
    trunk_hand_tree_.reset(new HandTree(0, 0, target_st_ - 1));
  }

  dynamic_cbr_.reset(new DynamicCBR());

#if 0
  // This is an inefficient way of computing the total number of raw
  // boards, but it doesn't really matter.  It's fast enough.
  unsigned int num_canon_boards = BoardTree::NumBoards(target_st_);
  unsigned int num_raw_boards = 0;
  for (unsigned int bd = 0; bd < num_canon_boards; ++bd) {
    num_raw_boards += BoardTree::BoardCount(target_st_, bd);
  }
#endif
  // unsigned int num_target_nodes =
  // NumTargetNodes(betting_tree_->Root(), target_st_);
  // num_subgames_ = num_target_nodes * num_raw_boards_;

  vector<unsigned int> v;
  unsigned int num_boards = BoardTree::NumBoards(target_st_);
  board_samples_.reset(new unsigned int[num_boards]);

  terminal_scaling_ = 0;
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    unsigned int board_count = BoardTree::BoardCount(target_st_, bd);
    unsigned int num_samples = 0;
    for (unsigned int i = 0; i < board_count; ++i) {
      if (RandZeroToOne() < sample_prob_) ++num_samples;
    }
    board_samples_[bd] = num_samples;
    // Ugh, need the number of sampled boards that don't conflict with
    // the particular hole cards in question?
    terminal_scaling_ += num_samples;
  }

  subgame_sumprobs_ = GetSumprobs();
}

ApproxRGBR::~ApproxRGBR(void) {
  delete subgame_sumprobs_;
}

double ApproxRGBR::GetBRVal(Node *node, double **reach_probs, unsigned int bd,
			    unsigned int p, CFRValues *sumprobs,
			    HandTree *hand_tree, double *br_norm) {
  double *cbrs = dynamic_cbr_->Compute(node, reach_probs, bd, hand_tree,
				       sumprobs, target_st_, bd, buckets_,
				       card_abstraction_, p, false, false);
  // hand_tree is local to this board
  const CanonicalCards *hands = hand_tree->Hands(target_st_, 0);
  double sum_joint_probs = 0;
  double sum_weighted_cbrs = 0;
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(target_st_);
  unsigned int maxcard1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    unsigned int our_enc = our_hi * maxcard1 + our_lo;
    double our_prob = reach_probs[p][our_enc];
    double sum_opp_probs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
      double opp_prob = reach_probs[p^1][opp_enc];
      sum_opp_probs += opp_prob;
    }
    // sum_opp_probs is already incorporated into the CBR values
    sum_joint_probs += our_prob * sum_opp_probs;
    sum_weighted_cbrs += our_prob * cbrs[i];
  }
  delete [] cbrs;
  *br_norm = sum_joint_probs;
  return sum_weighted_cbrs;
}

CFRValues *ApproxRGBR::GetSumprobs(void) {
  fprintf(stderr, "Reading sumprobs\n");
  unsigned int max_street = Game::MaxStreet();
  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), max_street,
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  }

#if 0
  // If I want to reduce memory usage, load sumprobs only for opponent.
  // Will have to do inside outer loop over players.  And then make sure
  // we sample the same boards.
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p1 = 0; p1 < num_players; ++p1) {
    players[p1] = p != p1;
  }
#endif
  unique_ptr<bool []> streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    streets[st] = (st >= target_st_);
  }
  // Pass in 0/0 for root_bd_st/root_bd.  We will have globally indexed
  // sumprobs.
  CFRValues *sumprobs =
    new CFRValues(nullptr, true, streets.get(), betting_tree_.get(), 0, 0,
		  card_abstraction_, buckets_, nullptr);
  sumprobs->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
  fprintf(stderr, "Read sumprobs\n");
  return sumprobs;

}

// I could do a lazy initialization of sumprobs when needed for the first
// time.
void ApproxRGBR::SampledCompute(Node *node, double **reach_probs,
				double *total_br_vals,
				double *total_br_norms) {
  // CFRValues *sumprobs = GetSumprobs();
  CFRValues *sumprobs = subgame_sumprobs_;
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_boards = BoardTree::NumBoards(target_st_);
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    unsigned int num_samples = board_samples_[bd];
    if (num_samples == 0) continue;
    fprintf(stderr, "Evaluating NT %u bd %u\n", node->NonterminalID(), bd);
    HandTree hand_tree(target_st_, bd, Game::MaxStreet());
    for (unsigned int p = 0; p < num_players; ++p) {
      double br_norm;
      double br_val = GetBRVal(node, reach_probs, bd, p, sumprobs, &hand_tree,
			       &br_norm);
      total_br_vals[p] += br_val * num_samples;
      total_br_norms[p] += br_norm * num_samples;
    }
  }
  // delete sumprobs;
}

void ApproxRGBR::AlwaysCallSampledCompute(Node *node, double **reach_probs,
					  double *total_br_vals,
					  double *total_br_norms) {
  CFRValues *sumprobs = GetSumprobs();
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_boards = BoardTree::NumBoards(target_st_);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(target_st_);
  double **my_reach_probs = new double *[2];
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  double *all_ones = new double[num_enc];

  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    unsigned int num_samples = board_samples_[bd];
    if (num_samples == 0) continue;
    fprintf(stderr, "Evaluating NT %u bd %u\n", node->NonterminalID(), bd);
    HandTree hand_tree(target_st_, bd, Game::MaxStreet());
    const CanonicalCards *hands = hand_tree.Hands(target_st_, 0);
    for (unsigned int i = 0; i < num_enc; ++i) all_ones[i] = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      unsigned int enc = cards[0] * max_card1 + cards[1];
      all_ones[enc] = 1.0;
    }
    for (unsigned int p = 0; p < num_players; ++p) {
      if (p == 0) {
	my_reach_probs[0] = all_ones;
	my_reach_probs[1] = reach_probs[1];
      } else {
	my_reach_probs[0] = reach_probs[0];
	my_reach_probs[1] = all_ones;
      }
      double br_norm;
      double br_val = GetBRVal(node, my_reach_probs, bd, p, sumprobs,
			       &hand_tree, &br_norm);
      total_br_vals[p] += br_val * num_samples;
      total_br_norms[p] += br_norm * num_samples;
    }
  }
  delete sumprobs;
  delete [] all_ones;
  delete [] my_reach_probs;
}

// bd is a global board index
double ***ApproxRGBR::GetSuccReachProbs(Node *node, unsigned int bd,
					double **reach_probs) {
  unsigned int num_succs = node->NumSuccs();
  double ***succ_reach_probs = new double **[num_succs];
  if (num_succs == 1) {
    succ_reach_probs[0] = reach_probs;
    return succ_reach_probs;
  }
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  for (unsigned int s = 0; s < num_succs; ++s) {
    succ_reach_probs[s] = new double *[2];
    for (unsigned int p = 0; p < 2; ++p) {
      succ_reach_probs[s][p] = new double[num_enc];
    }
  }
  unsigned int st = node->Street();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  if (nt >= trunk_sumprobs_->NumNonterminals(pa, st)) {
    fprintf(stderr, "GetSuccReachProbs: OOB nt %u numnt %u\n", nt,
	    trunk_sumprobs_->NumNonterminals(pa, st));
    exit(-1);
  }
  unsigned int dsi = node->DefaultSuccIndex();
  const CanonicalCards *hands = trunk_hand_tree_->Hands(st, bd);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    unsigned int offset;
    if (buckets_.None(st)) {
      offset = bd * num_hole_card_pairs * num_succs + i * num_succs;
    } else {
      unsigned int h = bd * num_hole_card_pairs + i;
      unsigned int b = buckets_.Bucket(st, h);
      offset = b * num_succs;
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      double prob =
	trunk_sumprobs_->Prob(pa, st, nt, offset, s, num_succs, dsi);
      if (prob > 1.0) {
	fprintf(stderr, "Prob > 1\n");
	fprintf(stderr, "num_succs %u bd %u nhcp %u hcp %u\n", num_succs,
		bd, num_hole_card_pairs, i);
	exit(-1);
      }
      for (unsigned int p = 0; p <= 1; ++p) {
	if (p == pa) {
	  succ_reach_probs[s][p][enc] = reach_probs[p][enc] * prob;
	} else {
	  succ_reach_probs[s][p][enc] = reach_probs[p][enc];
	}
      }
    }
  }
  
  return succ_reach_probs;
}

// A little tricky to get this just right.  We need to scale by the number
// of sampled raw flop boards in order for the flop values and the preflop
// values to be on the same scale.  But we are typically not sampling
// every flop board, so there is no simple scaling factor that we can
// apply.  We actually have to iterate over the sampled flop boards and
// do the terminal node calculation separately for each of those boards.
void ApproxRGBR::Terminal(Node *node, double **reach_probs,
			  double *total_br_vals, double *total_br_norms) {
  // unsigned int st = node->Street();
  unsigned int num_boards = BoardTree::NumBoards(target_st_);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(target_st_);
  unique_ptr<double []> total_card_probs(new double[num_hole_card_pairs]);
  unsigned int maxcard1 = Game::MaxCard() + 1;
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    unsigned int num_samples = board_samples_[bd];
    if (num_samples == 0) continue;
    HandTree hand_tree(target_st_, bd, target_st_);
    const CanonicalCards *hands = hand_tree.Hands(target_st_, 0);
    for (unsigned int p = 0; p < num_players; ++p) {
      double *opp_probs = reach_probs[p^1];
      double sum_opp_probs;
      CommonBetResponseCalcs(target_st_, hands, opp_probs, &sum_opp_probs,
			     total_card_probs.get());
      double *vals;
      if (node->Showdown()) {
	vals = Showdown(node, hands, opp_probs, sum_opp_probs,
			total_card_probs.get());
      } else {
	vals = Fold(node, p, hands, opp_probs, sum_opp_probs,
		    total_card_probs.get());
      }
      double sum_joint_probs = 0;
      double sum_weighted_vals = 0;
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	const Card *our_cards = hands->Cards(i);
	Card our_hi = our_cards[0];
	Card our_lo = our_cards[1];
	unsigned int our_enc = our_hi * maxcard1 + our_lo;
	double our_prob = reach_probs[p][our_enc];
	double sum_opp_probs = 0;
	for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
	  const Card *opp_cards = hands->Cards(j);
	  Card opp_hi = opp_cards[0];
	  Card opp_lo = opp_cards[1];
	  if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	      opp_lo == our_lo) {
	    continue;
	  }
	  unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
	  double opp_prob = reach_probs[p^1][opp_enc];
	  sum_opp_probs += opp_prob;
	}
	// sum_opp_probs is already incorporated into the values
	// Need to scale by number of times we sampled this canonical board.
	sum_joint_probs += our_prob * sum_opp_probs * num_samples;
	sum_weighted_vals += our_prob * vals[i] * num_samples;
      }
      total_br_vals[p] += sum_weighted_vals;
      total_br_norms[p] += sum_joint_probs;
      delete [] vals;
    }
  }
}

#if 0
void ApproxRGBR::Terminal(Node *node, unsigned int bd, double **reach_probs,
			  double *total_br_vals, double *total_br_norms) {
  unsigned int st = node->Street();
  const CanonicalCards *hands = trunk_hand_tree_->Hands(st, bd);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unique_ptr<double []> total_card_probs(new double[num_hole_card_pairs]);
  unsigned int maxcard1 = Game::MaxCard() + 1;
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    double *opp_probs = reach_probs[p^1];
    double sum_opp_probs;
    CommonBetResponseCalcs(st, hands, opp_probs, &sum_opp_probs,
			   total_card_probs.get());
    double *vals;
    if (node->Showdown()) {
      vals = Showdown(node, hands, opp_probs, sum_opp_probs,
		      total_card_probs.get());
    } else {
      vals = Fold(node, p, hands, opp_probs, sum_opp_probs,
		  total_card_probs.get());
    }
    double sum_joint_probs = 0;
    double sum_weighted_vals = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *our_cards = hands->Cards(i);
      Card our_hi = our_cards[0];
      Card our_lo = our_cards[1];
      unsigned int our_enc = our_hi * maxcard1 + our_lo;
      double our_prob = reach_probs[p][our_enc];
      double sum_opp_probs = 0;
      for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
	const Card *opp_cards = hands->Cards(j);
	Card opp_hi = opp_cards[0];
	Card opp_lo = opp_cards[1];
	if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	    opp_lo == our_lo) {
	  continue;
	}
	unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
	double opp_prob = reach_probs[p^1][opp_enc];
	sum_opp_probs += opp_prob;
      }
      // sum_opp_probs is already incorporated into the values
      // Need to scale terminal values up by number of sampled raw boards
      sum_joint_probs += our_prob * sum_opp_probs * terminal_scaling_;
      sum_weighted_vals += our_prob * vals[i] * terminal_scaling_;
    }
    delete [] vals;
    total_br_vals[p] += sum_weighted_vals;
    total_br_norms[p] += sum_joint_probs;
  }
}
#endif

void ApproxRGBR::Walk(Node *node, unsigned int bd, double **reach_probs,
		      double *total_br_vals, double *total_br_norms) {
  if (node->Terminal()) {
    if (! always_call_) {
      Terminal(node, reach_probs, total_br_vals, total_br_norms);
    }
    return;
  }
  unsigned int st = node->Street();
  if (st == target_st_) {
    if (always_call_) {
      AlwaysCallSampledCompute(node, reach_probs, total_br_vals,
			       total_br_norms);
    } else {
      SampledCompute(node, reach_probs, total_br_vals, total_br_norms);
    }
    return;
  }
  if (st == 1) {
    fprintf(stderr, "Need to handle street transition\n");
    exit(-1);
  }
  unsigned int num_succs = node->NumSuccs();
  double ***succ_reach_probs = GetSuccReachProbs(node, bd, reach_probs);
  for (unsigned int s = 0; s < num_succs; ++s) {
    Walk(node->IthSucc(s), bd, succ_reach_probs[s], total_br_vals,
	 total_br_norms);
  }
  if (num_succs > 1) {
    for (unsigned int s = 0; s < num_succs; ++s) {
      for (unsigned int p = 0; p < 2; ++p) {
	delete [] succ_reach_probs[s][p];
      }
      delete [] succ_reach_probs[s];
    }
  }
  delete [] succ_reach_probs;
}

void ApproxRGBR::Go(void) {
  unsigned int num_players = Game::NumPlayers();
  double **reach_probs = new double *[num_players];
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  const CanonicalCards *preflop_hands = trunk_hand_tree_->Hands(0, 0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  for (unsigned int p = 0; p < num_players; ++p) {
    reach_probs[p] = new double[num_enc];
    for (unsigned int i = 0; i < num_enc; ++i) reach_probs[p][i] = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = preflop_hands->Cards(i);
      unsigned int enc = cards[0] * max_card1 + cards[1];
      reach_probs[p][enc] = 1.0;
    }
  }
  unique_ptr<double []> total_br_vals(new double[2]);
  unique_ptr<double []> total_br_norms(new double[2]);
  for (unsigned int p = 0; p < num_players; ++p) {
    total_br_vals[p] = 0;
    total_br_norms[p] = 0;
  }
  Walk(betting_tree_->Root(), 0, reach_probs, total_br_vals.get(),
       total_br_norms.get());
  double gap = 0;
  for (unsigned int p = 0; p < num_players; ++p) {
    double est_br = total_br_vals[p] / total_br_norms[p];
    fprintf(stderr, "Est. P%u BR val: %f (norm %f)\n", p, est_br,
	    total_br_norms[p]);
    gap += est_br;
  }
  double est_expl = ((gap / 2.0) / 2.0) * 1000.0;
  fprintf(stderr, "Est. expl: %f mbb/g\n", est_expl);
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <num threads> <it> [current|avg] <sample prob> "
	  "[determ|nondeterm] (alwayscall)\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 10 && argc != 11) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_params = CreateCardAbstractionParams();
  card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    card_abstraction(new CardAbstraction(*card_params));
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[3]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unique_ptr<Params> cfr_params = CreateCFRParams();
  cfr_params->ReadFromFile(argv[4]);
  unique_ptr<CFRConfig> cfr_config(new CFRConfig(*cfr_params));
  unsigned int num_threads, it;
  if (sscanf(argv[5], "%u", &num_threads) != 1) Usage(argv[0]);
  if (sscanf(argv[6], "%u", &it) != 1)          Usage(argv[0]);
  string carg = argv[7];
  bool current;
  if (carg == "current")  current = true;
  else if (carg == "avg") current = false;
  else                    Usage(argv[0]);
  if (current) {
    fprintf(stderr, "Current not supported yet\n");
    exit(-1);
  }
  double sample_prob;
  bool determ;
  if (sscanf(argv[8], "%lf", &sample_prob) != 1) Usage(argv[0]);
  string darg = argv[9];
  if (darg == "determ")         determ = true;
  else if (darg == "nondeterm") determ = false;
  else                          Usage(argv[0]);
  bool always_call = false;
  if (argc == 11) {
    string ac_arg = argv[10];
    if (ac_arg != "alwayscall") Usage(argv[0]);
    always_call = true;
  }

  if (determ) {
    InitRandFixed();
  } else {
    InitRand();
  }
  
  BoardTree::Create();
  BoardTree::BuildBoardCounts();
  ApproxRGBR rgbr(*card_abstraction, *betting_abstraction, *cfr_config, it,
		  sample_prob, always_call);
  rgbr.Go();
}
