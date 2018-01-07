// Should support two methods of sampling.
// 1) Sample in proportion to joint-reach-probability of subgame.
// 2) Sample every subgame with equal probability.
// (2) may be preferred if the mere act of computing the joint-reach-probs at
// at every subgame is too expensive.
// Do I want to pass p0 and p1 probs down?  That implies we iterate over boards
// at street-initial nodes.  Alternatively, whenever we sample an endgame,
// then we calculate the reach probs.
//
// Need to scale different streets differently.
//
// Tests:
// 1) Run bot against itself.
// 2) Sample everything; see if results match play_agents.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
// #include "canonical.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_utils.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "params.h"
#include "rand.h"
#include "runtime_config.h"
#include "runtime_params.h"
#include "sorting.h"
#include "split.h"
#include "nl_agent.h"

class Player {
public:
  Player(NLAgent *a_agent, NLAgent *b_agent, BettingTree **a_betting_trees,
	 BettingTree **b_betting_trees, double sample_prob);
  ~Player(void);
  void Go(unsigned int a_player);
private:
  double SubgameShowdown(Node *node, double *p0_probs, double *p1_probs);
  double SubgameFold(Node *node, double *p0_probs, double *p1_probs);
  double EvaluateSubgame(Node *a_node, Node *b_node, unsigned int gbd,
			 double *p0_probs, double *p1_probs);
  double Subgame(Node *a_node, Node *b_node, unsigned int gbd,
		 double *p0_probs, double *p1_probs);
  void StreetInitial(Node *a_node, Node *b_node, unsigned int pgbd,
		     const CanonicalCards *prev_hands,
		     double *p0_probs, double *p1_probs, double *val,
		     double *norm);
  void Showdown(Node *node, const CanonicalCards *hands, double *p0_probs,
		double *p1_probs, double *val, double *norm);
  void Fold(Node *node, const CanonicalCards *hands, double *p0_probs,
	    double *p1_probs, double *val, double *norm);
  void Walk(Node *a_node, Node *b_node, unsigned int gbd,
	    CanonicalCards *hands, double *p0_probs, double *p1_probs,
	    unsigned int last_st, double *val, double *norm);
  
  unsigned int subgame_st_;
  double sample_prob_;
  NLAgent *a_agent_;
  NLAgent *b_agent_;
  BettingTree **a_betting_trees_;
  BettingTree **b_betting_trees_;
  unsigned int a_player_;
};

Player::Player(NLAgent *a_agent, NLAgent *b_agent,
	       BettingTree **a_betting_trees, BettingTree **b_betting_trees,
	       double sample_prob) {
  a_agent_ = a_agent;
  b_agent_ = b_agent;
  a_betting_trees_ = a_betting_trees;
  b_betting_trees_ = b_betting_trees;
  sample_prob_ = sample_prob;
}

Player::~Player(void) {
}

double Player::SubgameShowdown(Node *node, double *p0_probs,
			       double *p1_probs) {
  return 0;
}

double Player::SubgameFold(Node *node, double *p0_probs, double *p1_probs) {
  return 0;
}

double Player::EvaluateSubgame(Node *a_node, Node *b_node, unsigned int gbd,
			       double *p0_probs, double *p1_probs) {
  return 0;
}

double Player::Subgame(Node *a_node, Node *b_node, unsigned int gbd,
		       double *p0_probs, double *p1_probs) {
  unsigned int st = a_node->Street();
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int board_count = BoardTree::BoardCount(st, gbd);
  unsigned int max_card1 = Game::MaxCard() + 1;
  HandTree hand_tree(st, gbd, max_street);
  const CanonicalCards *hands = hand_tree.Hands(st, 0);
  double sum_joint_probs = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *p0_cards = hands->Cards(i);
    unsigned int p0_enc = p0_cards[0] * max_card1 + p0_cards[1];
    double p0_prob = p0_probs[p0_enc];
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *p1_cards = hands->Cards(j);
      if (p1_cards[0] == p0_cards[0] || p1_cards[0] == p0_cards[1] ||
	  p1_cards[1] == p0_cards[0] || p1_cards[1] == p0_cards[1]) {
	continue;
      }
      unsigned int p1_enc = p1_cards[0] * max_card1 + p1_cards[1];
      double p1_prob = p1_probs[p1_enc];
      sum_joint_probs += p0_prob * p1_prob;
    }
  }
  unsigned int num_samples = 0;
  for (unsigned int i = 0; i < board_count; ++i) {
    double r = RandZeroToOne();
    // Wait, this is wrong.  Higher sum_joint_probs should mean higher
    // probability of being sampled.
    if (r * sum_joint_probs < sample_prob_) ++num_samples;
  }
  if (num_samples == 0) {
    return 0;
  } else {
    return num_samples *
      EvaluateSubgame(a_node, b_node, gbd, p0_probs, p1_probs);
  }
}

void Player::StreetInitial(Node *a_node, Node *b_node, unsigned int pgbd,
			   const CanonicalCards *prev_hands,
			   double *p0_probs, double *p1_probs, double *val,
			   double *norm) {
  unsigned int nst = a_node->Street();
  unsigned int pst = nst - 1;
  unsigned int num_board_cards = Game::NumBoardCards(nst);
  unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
  unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
  double sum_vals = 0, sum_norms = 0;
  for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
    const Card *board = BoardTree::Board(nst, ngbd);
    unsigned int sg = BoardTree::SuitGroups(nst, ngbd);
    CanonicalCards next_hands(2, board, num_board_cards, sg, false);
    if (nst == Game::MaxStreet()) {
      next_hands.SortByHandStrength(board);
    }
    double next_val, next_norm;
    Walk(a_node, b_node, ngbd, &next_hands, p0_probs, p1_probs, nst, &next_val,
	 &next_norm);
    sum_vals += next_val;
    sum_norms += next_norm;
  }
  *val = sum_vals;
  *norm = sum_norms;
}

void Player::Showdown(Node *node, const CanonicalCards *hands,
		      double *p0_probs, double *p1_probs, double *val,
		      double *norm) {
  unsigned int st = node->Street();
  unsigned int max_card1 = Game::MaxCard() + 1;
  unique_ptr<double []> total_card_probs(new double[max_card1]);
  double sum_opp_probs;
  CommonBetResponseCalcs(st, hands, p0_probs, &sum_opp_probs,
			 total_card_probs.get());

  double cum_prob = 0;
  double cum_card_probs[52];
  for (Card c = 0; c < max_card1; ++c) cum_card_probs[c] = 0;
  unsigned int num_hole_card_pairs = hands->NumRaw();
  unique_ptr<double []> win_probs(new double[num_hole_card_pairs]);
  double half_pot = node->LastBetTo();
  double sum_p1_vals = 0;
  double sum_norm = 0;

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
      unsigned int enc = hi * max_card1 + lo;
      double prob = p0_probs[enc];
      cum_card_probs[hi] += prob;
      cum_card_probs[lo] += prob;
      cum_prob += prob;
    }
    for (unsigned int k = begin_range; k < j; ++k) {
      const Card *cards = hands->Cards(k);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double our_prob = p1_probs[enc];
      if (our_prob == 0) continue;
      
      double better_hi_prob = total_card_probs[hi] - cum_card_probs[hi];
      double better_lo_prob = total_card_probs[lo] - cum_card_probs[lo];
      double lose_prob = (sum_opp_probs - cum_prob) -
	better_hi_prob - better_lo_prob;
      double p1_val = (win_probs[k] - lose_prob) * half_pot;
      // sum_compat_opp_probs is the sum of the reach probabilities of
      // the opponent hands that don't conflict with our current hand
      // (<hi, lo>).
      double this_opp_prob = p0_probs[enc];
      double sum_compat_opp_probs = (sum_opp_probs -
	    (total_card_probs[hi] - total_card_probs[lo])) + this_opp_prob;
      // double norm_p1_val = p1_val / sum_compat_opp_probs;
      sum_p1_vals += our_prob * p1_val;
      sum_norm += our_prob * sum_compat_opp_probs;
    }
  }

  *val = sum_p1_vals;
  *norm = sum_norm;
}

void Player::Fold(Node *node, const CanonicalCards *hands, double *p0_probs,
		  double *p1_probs, double *val, double *norm) {
  unsigned int st = node->Street();
  unsigned int max_card1 = Game::MaxCard() + 1;
  unique_ptr<double []> total_card_probs(new double[max_card1]);
  double sum_p1_vals = 0;
  double sum_norm = 0;

  double sum_opp_probs;
  CommonBetResponseCalcs(st, hands, p0_probs, &sum_opp_probs,
			 total_card_probs.get());

  // Sign of half_pot reflects who wins the pot
  double half_pot;
  // Player acting encodes player remaining at fold nodes
  // LastBetTo() doesn't include the last called bet
  if (node->PlayerActing() == 1) {
    half_pot = node->LastBetTo();
  } else {
    half_pot = -(double)node->LastBetTo();
  }
  unsigned int num_hole_card_pairs = hands->NumRaw();

  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    double our_prob = p1_probs[enc];
    if (our_prob == 0) continue;
    double opp_prob = p0_probs[enc];
    double sum_compat_opp_probs = sum_opp_probs + opp_prob -
      (total_card_probs[hi] - total_card_probs[lo]);
    sum_p1_vals += our_prob * half_pot * sum_compat_opp_probs;
    sum_norm += our_prob * sum_compat_opp_probs;
  }
  
  *val = sum_p1_vals;
  *norm = sum_norm;
}

// Showdown() and Fold() get the pot size from the node passed in.  If we
// do translation, how do we know the correct pot size?
void Player::Walk(Node *a_node, Node *b_node, unsigned int gbd,
		  CanonicalCards *hands, double *p0_probs, double *p1_probs,
		  unsigned int last_st, double *val, double *norm) {
  unsigned int st = a_node->Street();
  fprintf(stderr, "Walk st %u\n", st);
  if (st > last_st) {
    StreetInitial(a_node, b_node, gbd, hands, p0_probs, p1_probs, val, norm);
    return;
  }
  if (st == subgame_st_) {
    double ret = Subgame(a_node, b_node, gbd, p0_probs, p1_probs);
    *val = ret;
    // TODO
    *norm = 0;
  }
  // Should I sample only some terminal nodes?
  if (a_node->Terminal()) {
    if (a_node->Showdown()) {
      fprintf(stderr, "Showdown\n");
      Showdown(a_node, hands, p0_probs, p1_probs, val, norm);
    } else {
      fprintf(stderr, "Fold\n");
      Fold(a_node, hands, p0_probs, p1_probs, val, norm);
    }
    return;
  }
  unsigned int pa = a_node->PlayerActing();
  bool a_active = (a_player_ == pa);
  Node *node = a_active ? a_node : b_node;
  unsigned int num_succs = node->NumSuccs();
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  unique_ptr<double []> incr_probs(new double[num_enc]);
  unique_ptr<double []> new_probs(new double[num_enc]);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  double sum_vals = 0, sum_norms = 0;
  for (unsigned int s = 0; s < num_succs; ++s) {
    Node *a_succ, *b_succ;
    // For now, assume identical betting abstractions in the trunk
    if (a_active) {
      a_agent_->AllProbs(a_node, s, gbd, hands, pa, incr_probs.get());
      a_succ = a_node->IthSucc(s);
      b_succ = b_node->IthSucc(s);
    } else {
      b_agent_->AllProbs(b_node, s, gbd, hands, pa, incr_probs.get());
      b_succ = b_node->IthSucc(s);
      a_succ = a_node->IthSucc(s);
    }
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      unsigned int enc = cards[0] * max_card1 + cards[1];
      if (pa == 0) {
	new_probs[enc] = p0_probs[enc] * incr_probs[enc];
      } else {
	new_probs[enc] = p1_probs[enc] * incr_probs[enc];
      }
    }
    double succ_val, succ_norm;
    if (pa == 0) {
      Walk(a_succ, b_succ, gbd, hands, new_probs.get(), p1_probs, st,
	   &succ_val, &succ_norm);
    } else {
      Walk(a_succ, b_succ, gbd, hands, p0_probs, new_probs.get(), st,
	   &succ_val, &succ_norm);
    }
    sum_vals += succ_val;
    sum_norms += succ_norm;
  }
  *val = sum_vals;
  *norm = sum_norms;
}

void Player::Go(unsigned int a_player) {
  a_player_ = a_player;
  Node *a_root = a_betting_trees_[a_player_]->Root();
  // Assumes heads-up
  Node *b_root = b_betting_trees_[a_player_^1]->Root();
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  unique_ptr<double []> p0_probs(new double[num_enc]);
  unique_ptr<double []> p1_probs(new double[num_enc]);
  for (unsigned int i = 0; i < num_enc; ++i) {
    p0_probs[i] = 1.0;
    p1_probs[i] = 1.0;
  }
  CanonicalCards hands(2, nullptr, 0, 0, false);
  // Need to get back normalization term?
  double val, norm;
  Walk(a_root, b_root, 0, &hands, p0_probs.get(), p1_probs.get(), 0, &val,
       &norm);
  double norm_val = val / norm;
  printf("A player %u P1 val: %f\n", a_player_, norm_val);
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <A base card abstraction params> "
	  "<A endgame card abstraction params> "
	  "<A base betting abstraction params> "
	  "<A endgame betting abstraction params> <A base CFR params> "
	  "<A endgame CFR params> <A runtime params> <A endgame st> "
	  "<A num endgame its> <A its> <B base card abstraction params> "
	  "<B endgame card abstraction params> "
	  "<B base betting abstraction params> "
	  "<B endgame betting abstraction params> <B base CFR params> "
	  "<B endgame CFR params> <B runtime params> <B endgame st> "
	  "<B num endgame its> <B its> <sample prob>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc < 23) {
    Usage(argv[0]);
  }

  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);

  unique_ptr<Params> a_base_card_params = CreateCardAbstractionParams();
  a_base_card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    a_base_card_abstraction(new CardAbstraction(*a_base_card_params));
  unique_ptr<Params> a_endgame_card_params = CreateCardAbstractionParams();
  a_endgame_card_params->ReadFromFile(argv[3]);
  unique_ptr<CardAbstraction>
    a_endgame_card_abstraction(new CardAbstraction(*a_endgame_card_params));
  unique_ptr<Params> a_base_betting_params = CreateBettingAbstractionParams();
  a_base_betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    a_base_betting_abstraction(new BettingAbstraction(*a_base_betting_params));
  unique_ptr<Params> a_endgame_betting_params =
    CreateBettingAbstractionParams();
  a_endgame_betting_params->ReadFromFile(argv[5]);
  unique_ptr<BettingAbstraction>
    a_endgame_betting_abstraction(
			  new BettingAbstraction(*a_endgame_betting_params));
  unique_ptr<Params> a_base_cfr_params = CreateCFRParams();
  a_base_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig> a_base_cfr_config(new CFRConfig(*a_base_cfr_params));
  unique_ptr<Params> a_endgame_cfr_params = CreateCFRParams();
  a_endgame_cfr_params->ReadFromFile(argv[7]);
  unique_ptr<CFRConfig> a_endgame_cfr_config(
			    new CFRConfig(*a_endgame_cfr_params));
  unique_ptr<Params> a_runtime_params = CreateRuntimeParams();
  a_runtime_params->ReadFromFile(argv[8]);
  unique_ptr<RuntimeConfig>
    a_runtime_config(new RuntimeConfig(*a_runtime_params));
  unsigned int a_endgame_st, a_num_endgame_its;
  if (sscanf(argv[9], "%u", &a_endgame_st) != 1) Usage(argv[0]);
  if (sscanf(argv[10], "%u", &a_num_endgame_its) != 1) Usage(argv[0]);
  unsigned int num_players = Game::NumPlayers();
  if (num_players > 2) {
    fprintf(stderr, "Multiplayer not supported yet\n");
    exit(-1);	
  }
  unsigned int *a_iterations = new unsigned int[num_players];
  unsigned int a = 11;
  if (a_base_betting_abstraction->Asymmetric()) {
    unsigned int it;
    for (unsigned int p = 0; p < num_players; ++p) {
      if ((int)a >= argc) Usage(argv[0]);
      if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
      a_iterations[p] = it;
    }
  } else {
    unsigned int it;
    if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
    for (unsigned int p = 0; p < num_players; ++p) {
      a_iterations[p] = it;
    }
  }

  unique_ptr<Params> b_base_card_params = CreateCardAbstractionParams();
  b_base_card_params->ReadFromFile(argv[a++]);
  unique_ptr<CardAbstraction>
    b_base_card_abstraction(new CardAbstraction(*b_base_card_params));
  unique_ptr<Params> b_endgame_card_params = CreateCardAbstractionParams();
  b_endgame_card_params->ReadFromFile(argv[a++]);
  unique_ptr<CardAbstraction>
    b_endgame_card_abstraction(new CardAbstraction(*b_endgame_card_params));
  unique_ptr<Params> b_base_betting_params = CreateBettingAbstractionParams();
  b_base_betting_params->ReadFromFile(argv[a++]);
  unique_ptr<BettingAbstraction>
    b_base_betting_abstraction(new BettingAbstraction(*b_base_betting_params));
  unique_ptr<Params> b_endgame_betting_params =
    CreateBettingAbstractionParams();
  b_endgame_betting_params->ReadFromFile(argv[a++]);
  unique_ptr<BettingAbstraction>
    b_endgame_betting_abstraction(
			  new BettingAbstraction(*b_endgame_betting_params));
  unique_ptr<Params> b_base_cfr_params = CreateCFRParams();
  b_base_cfr_params->ReadFromFile(argv[a++]);
  unique_ptr<CFRConfig> b_base_cfr_config(new CFRConfig(*b_base_cfr_params));
  unique_ptr<Params> b_endgame_cfr_params = CreateCFRParams();
  b_endgame_cfr_params->ReadFromFile(argv[a++]);
  unique_ptr<CFRConfig> b_endgame_cfr_config(
			    new CFRConfig(*b_endgame_cfr_params));
  unique_ptr<Params> b_runtime_params = CreateRuntimeParams();
  b_runtime_params->ReadFromFile(argv[a++]);
  unique_ptr<RuntimeConfig>
    b_runtime_config(new RuntimeConfig(*b_runtime_params));
  unsigned int b_endgame_st, b_num_endgame_its;
  if (sscanf(argv[a++], "%u", &b_endgame_st) != 1) Usage(argv[0]);
  if (sscanf(argv[a++], "%u", &b_num_endgame_its) != 1) Usage(argv[0]);
  unsigned int *b_iterations = new unsigned int[num_players];
  if (b_base_betting_abstraction->Asymmetric()) {
    unsigned int it;
    for (unsigned int p = 0; p < num_players; ++p) {
      if ((int)a >= argc) Usage(argv[0]);
      if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
      b_iterations[p] = it;
    }
  } else {
    unsigned int it;
    if (sscanf(argv[a++], "%u", &it) != 1) Usage(argv[0]);
    for (unsigned int p = 0; p < num_players; ++p) {
      b_iterations[p] = it;
    }
  }

  double sample_prob;
  if (sscanf(argv[a++], "%lf", &sample_prob) != 1) Usage(argv[0]);

  InitRand();

  BoardTree::Create();
  BoardTree::CreateLookup();
  BoardTree::BuildBoardCounts();
  HandValueTree::Create();

  BettingTree **a_betting_trees = new BettingTree *[num_players];
  if (a_base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      a_betting_trees[p] =
	BettingTree::BuildAsymmetricTree(*a_base_betting_abstraction, p);
    }
  } else {
    BettingTree *a_betting_tree =
      BettingTree::BuildTree(*a_base_betting_abstraction);
    for (unsigned int p = 0; p < num_players; ++p) {
      a_betting_trees[p] = a_betting_tree;
    }
  }

  BettingTree **b_betting_trees = new BettingTree *[num_players];
  if (b_base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      b_betting_trees[p] =
	BettingTree::BuildAsymmetricTree(*b_base_betting_abstraction, p);
    }
  } else {
    BettingTree *b_betting_tree =
      BettingTree::BuildTree(*b_base_betting_abstraction);
    for (unsigned int p = 0; p < num_players; ++p) {
      b_betting_trees[p] = b_betting_tree;
    }
  }

  // No fixed seed; will handle seeding here.
  unsigned int small_blind = 50;
  unsigned int stack_size = 20000;
  bool fixed_seed = true;
  bool debug = false;
  bool exit_on_error = true;
  NLAgent a_agent(*a_base_card_abstraction, *a_endgame_card_abstraction,
		  *a_base_betting_abstraction, *a_endgame_betting_abstraction,
		  *a_base_cfr_config, *a_endgame_cfr_config, *a_runtime_config,
		  a_iterations, a_betting_trees, a_endgame_st,
		  a_num_endgame_its, debug, exit_on_error, fixed_seed,
		  small_blind, stack_size);
  NLAgent b_agent(*b_base_card_abstraction, *b_endgame_card_abstraction,
		  *b_base_betting_abstraction, *b_endgame_betting_abstraction,
		  *b_base_cfr_config, *b_endgame_cfr_config, *b_runtime_config,
		  b_iterations, b_betting_trees, b_endgame_st,
		  b_num_endgame_its, debug, exit_on_error, fixed_seed,
		  small_blind, stack_size);

  Player player(&a_agent, &b_agent, a_betting_trees, b_betting_trees,
		sample_prob);

  for (unsigned int p = 0; p < num_players; ++p) {
    player.Go(p);
  }

  if (a_base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete a_betting_trees[p];
    }
  } else {
    delete a_betting_trees[0];
  }
  delete [] a_betting_trees;

  if (b_base_betting_abstraction->Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete b_betting_trees[p];
    }
  } else {
    delete b_betting_trees[0];
  }
  delete [] b_betting_trees;
}
