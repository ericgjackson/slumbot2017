// This version of play allows one player to solve endgames on the fly.
//
// Do I need base betting tree?  Maybe for subtree nt?

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "canonical_cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "eg_cfr.h"
#include "endgames.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "params.h"
#include "path.h"
#include "rand.h"
#include "runtime_config.h"
#include "runtime_params.h"
#include "sorting.h"

using namespace std;

class Player {
public:
  Player(BettingTree *base_betting_tree, BettingTree *merged_betting_tree,
	 const BettingAbstraction &base_ba,
	 const BettingAbstraction &merged_ba, const CFRConfig &a_cc,
	 const CFRConfig &b_cc, const RuntimeConfig &a_rc,
	 const RuntimeConfig &b_rc);
  ~Player(void);
  void Go(unsigned long long int num_duplicate_hands, bool deterministic);
private:
  void SetHCPsAndBoards(const Card *p1_hand_cards, const Card *p2_hand_cards);
  void Resolve(const Node *base_subtree_root, const Path &merged_path,
	       bool p1, BettingTree *subtree);
  int Play(Node *base_node, Path *merged_path, bool a_is_p1);
  int PlayDuplicateHand(unsigned int h, const Card *cards,
			bool deterministic);

  BettingTree *base_betting_tree_;
  BettingTree *merged_betting_tree_;
  const BettingAbstraction &base_ba_;
  const BettingAbstraction &merged_ba_;
  const CFRConfig &a_cc_;
  const CFRConfig &b_cc_;
  unsigned int resolve_st_;
  unique_ptr<CFRValues> a_trunk_probs_;
  unique_ptr<CFRValues> b_probs_;
  unique_ptr<CFRValues> a_resolve_probs_;
  unsigned int *boards_;
  unsigned int *p1_hcps_;
  unsigned int *p2_hcps_;
  int p1_showdown_;
  unsigned int **sorted_hcps_;
  EndgameSolver *endgame_solver_;
  long long int sum_p1_outcomes_;
};

// Does a_probs_ hold probabilities for the whole tree or just for the
// current subgame?  If for the whole tree, I think I need a Read() method
// to read in just a subgame.  Current Read() method only supports reading
// all of the values.
// Easier maybe to have a_trunk_probs_ and a_subgame_probs_.  Need to
// construct a_subgame_probs_ here.
void Player::Resolve(const Node *base_subtree_root, const Path &merged_path,
		     bool p1, BettingTree *subtree) {
  unsigned int bd = boards_[resolve_st_];
  Node *merged_subtree_root = merged_path.Last();
  unsigned int base_subtree_nt = base_subtree_root->NonterminalID();
  unsigned int max_street = Game::MaxStreet();
  // Number of iterations should be a parameter
  unsigned int num_its = 200;
  endgame_solver_->Solve(merged_path.Nodes(), bd, base_subtree_nt, num_its,
			 p1, ! p1);
  char dir[500];
  sprintf(dir, "%s/%s.null.%u.%u.%u.%s.%s/endgames.%s.%u",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_ba_.BettingAbstractionName().c_str(),
	  a_cc_.CFRConfigName().c_str(),
	  merged_ba_.BettingAbstractionName().c_str(), resolve_st_);
  bool *streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    streets[st] = st >= resolve_st_;
  }
  a_resolve_probs_.reset(new CFRValues(p1, ! p1, true, streets,
				       subtree->NumNonterminals(), bd,
				       resolve_st_, kMaxUInt));
  delete [] streets;
  a_resolve_probs_->Read(dir, num_its, subtree->Root(),
			 merged_subtree_root->NonterminalID(),
			 p1 ? 1 : 0);
}

// Returns outcomes from P1's perspective
int Player::Play(Node *base_node, Path *merged_path, bool a_is_p1) {
  Node *merged_node = merged_path->Last();
  unsigned int st = merged_node->Street();
  if (base_node && st == resolve_st_) {
    BettingTree *subtree = BettingTree::BuildSubtree(merged_node);
    Resolve(base_node, *merged_path, a_is_p1, subtree);
    merged_path->SetLastNode(subtree->Root());
    int ret = Play(nullptr, merged_path, a_is_p1);
    delete subtree;
    a_resolve_probs_.reset(nullptr);
    return ret;
  }
  if (merged_node->Terminal()) {
    if (merged_node->P1Fold()) {
      int ret = -((int)merged_node->PotSize()) / 2;
      return ret;
    } else if (merged_node->P2Fold()) {
      int ret = ((int)merged_node->PotSize()) / 2;
      return ret;
    } else {
      // Showdown
      int ret = p1_showdown_ * ((int)merged_node->PotSize() / 2);
      return ret;
    }
  } else {
    unsigned int nt = merged_node->NonterminalID();
    unsigned int num_succs = merged_node->NumSuccs();
    unsigned int p1 = merged_node->P1Choice();
    bool a_is_acting = a_is_p1 == p1;
    const CFRValues &probs = a_is_acting ?
      (st >= resolve_st_ ? *a_resolve_probs_ : *a_trunk_probs_) : *b_probs_;
    unsigned int dsi = merged_node->DefaultSuccIndex();
    unsigned int bd = boards_[st];
    if (a_is_acting && st >= resolve_st_) {
      // We are within a subtree.  Change bd to the local board index.
      bd = BoardTree::LocalIndex(resolve_st_, boards_[resolve_st_], st, bd);
    }
    unsigned int hcp = p1 ? p1_hcps_[st] : p2_hcps_[st];
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    double r = RandZeroToOne();
    double cum = 0;
    int s;
    for (s = 0; s < ((int)num_succs) - 1; ++s) {
      double prob;
      // Board should be local board ID
      prob = probs.Prob(p1, st, nt, bd, hcp, num_hole_card_pairs, s,
			num_succs, dsi);
      cum += prob;
      if (r < cum) break;
    }
    int ret;
    if (base_node) {
      merged_path->SetLastSucc(s);
      merged_path->Push(merged_node->IthSucc(s));
      ret = Play(base_node->IthSucc(s), merged_path, a_is_p1);
      merged_path->Pop();
    } else {
    }
    return ret;
  }
}

// Play one hand of duplicate, which is a pair of regular hands.  Return
// outcome from A's perspective.
int Player::PlayDuplicateHand(unsigned int h, const Card *cards,
			      bool deterministic) {
  int a_sum = 0;
  for (unsigned int a_is_p1 = 0; a_is_p1 <= 1; ++a_is_p1) {
#if 0
    if (deterministic) {
      // Reseed the RNG again before play within this loop.  This ensure
      // that if we play a system against itself, the duplicate outcome will
      // always be zero.
      //
      // This has a big impact on the average P1 outcome - why?  Too much
      // coordination between the RNG for dealing the cards and the RNG for
      // actions?
      SeedRand(h);
    }
#endif
    Path merged_path;
    merged_path.Push(merged_betting_tree_->Root());
    int p1_outcome = Play(base_betting_tree_->Root(), &merged_path, a_is_p1);
    if (a_is_p1) {
      a_sum += p1_outcome;
    } else {
      a_sum -= p1_outcome;
    }
    sum_p1_outcomes_ += p1_outcome;
  }
  return a_sum;
}

static void DealNCards(Card *cards, unsigned int n) {
  unsigned int max_card = Game::MaxCard();
  for (unsigned int i = 0; i < n; ++i) {
    Card c;
    while (true) {
      c = RandBetween(0, max_card);
      unsigned int j;
      for (j = 0; j < i; ++j) {
	if (cards[j] == c) break;
      }
      if (j == i) break;
    }
    cards[i] = c;
  }
}

void Player::SetHCPsAndBoards(const Card *p1_hand_cards,
			      const Card *p2_hand_cards) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (st == 0) {
      p1_hcps_[0] = HCPIndex(st, p1_hand_cards);
      p2_hcps_[0] = HCPIndex(st, p2_hand_cards);
    } else {
      // Store the hole cards *after* the board cards
      unsigned int num_hole_cards = Game::NumCardsForStreet(0);
      Card p1_raw_cards[7], p1_canon_cards[7];
      Card p2_raw_cards[7], p2_canon_cards[7];
      unsigned int num_board_cards = Game::NumBoardCards(st);
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	p1_raw_cards[i] = p1_hand_cards[i + num_hole_cards];
	p2_raw_cards[i] = p2_hand_cards[i + num_hole_cards];
      }
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	p1_raw_cards[num_board_cards + i] = p1_hand_cards[i];
	p2_raw_cards[num_board_cards + i] = p2_hand_cards[i];
      }
      Fix
      bool p1_change_made = 
	CanonicalCards::ToCanon2(p1_raw_cards, num_board_cards + num_hole_cards,
				 0, p1_canon_cards);
      if (p1_change_made) {
	num_board_cards = 0;
	for (unsigned int st1 = 1; st1 <= st; ++st1) {
	  unsigned int num_street_cards = Game::NumCardsForStreet(st1);
	  SortCards(p1_canon_cards + num_board_cards, num_street_cards);
	  num_board_cards += num_street_cards;
	}
	SortCards(p1_canon_cards + num_board_cards, num_hole_cards);
      }
      bool p2_change_made = 
	CanonicalCards::ToCanon2(p2_raw_cards, num_board_cards + num_hole_cards,
				 0, p2_canon_cards);
      if (p2_change_made) {
	num_board_cards = 0;
	for (unsigned int st1 = 1; st1 <= st; ++st1) {
	  unsigned int num_street_cards = Game::NumCardsForStreet(st1);
	  SortCards(p2_canon_cards + num_board_cards, num_street_cards);
	  num_board_cards += num_street_cards;
	}
	SortCards(p2_canon_cards + num_board_cards, num_hole_cards);
      }
      unsigned int bd = BoardTree::LookupBoard(p1_canon_cards, st);
      boards_[st] = bd;
      // Put the hole cards back at the beginning
      Card canon_cards2[7];
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	canon_cards2[num_hole_cards + i] = p1_canon_cards[i];
      }
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	canon_cards2[i] = p1_canon_cards[num_board_cards + i];
      }
      p1_hcps_[st] = HCPIndex(st, canon_cards2);
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	canon_cards2[i] = p2_canon_cards[num_board_cards + i];
      }
      p2_hcps_[st] = HCPIndex(st, canon_cards2);
    }
  }
  // On the final street we need the sorted HCP index (the index into the
  // vector of hole card pairs that has been sorted by hand strength).
  unsigned int msbd = boards_[max_street];
  p1_hcps_[max_street] = sorted_hcps_[msbd][p1_hcps_[max_street]];
  p2_hcps_[max_street] = sorted_hcps_[msbd][p2_hcps_[max_street]];
}

void Player::Go(unsigned long long int num_duplicate_hands,
		bool deterministic) {
  // From perspective of player A
  long long int sum_pair_outcomes = 0;
  long long int sum_sqd_pair_outcomes = 0;
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_board_cards = Game::NumBoardCards(max_street);
  Card cards[9], p1_hand_cards[7], p2_hand_cards[7];
  if (! deterministic) {
    InitRand();
  }
  for (unsigned int h = 0; h < num_duplicate_hands; ++h) {
    if (deterministic) {
      // Seed just as we do in play_agents so we can get the same cards and
      // compare results.
      SeedRand(h);
    }
    // Assume 2 hole cards
    DealNCards(cards, num_board_cards + 4);
    SortCards(cards, 2);
    SortCards(cards + 2, 2);
    unsigned int num = 4;
    for (unsigned int st = 1; st <= max_street; ++st) {
      unsigned int num_street_cards = Game::NumCardsForStreet(st);
      SortCards(cards + num, num_street_cards);
      num += num_street_cards;
    }
    // OutputNCards(cards, num_board_cards + 4);
    // printf("\n");
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      p1_hand_cards[i + 2] = cards[i + 4];
      p2_hand_cards[i + 2] = cards[i + 4];
    }
    p1_hand_cards[0] = cards[0];
    p1_hand_cards[1] = cards[1];
    unsigned int p1_hv = HandValueTree::Val(p1_hand_cards);
    p2_hand_cards[0] = cards[2];
    p2_hand_cards[1] = cards[3];
    unsigned int p2_hv = HandValueTree::Val(p2_hand_cards);
    SetHCPsAndBoards(p1_hand_cards, p2_hand_cards);
    if (p1_hv > p2_hv)      p1_showdown_ = 1;
    else if (p2_hv > p1_hv) p1_showdown_ = -1;
    else                    p1_showdown_ = 0;
    // PlayDuplicateHand() returns the result of a pair of hands
    int pair_outcome = PlayDuplicateHand(h, cards, deterministic);
    sum_pair_outcomes += pair_outcome;
    sum_sqd_pair_outcomes += pair_outcome * pair_outcome;
  }
  double mean_pair_outcome = sum_pair_outcomes / (double)num_duplicate_hands;
  // Need to divide by two twice:
  // 1) Once to convert pair outcomes to hand outcomes.
  // 2) A second time to convert from small blind units to big blind units
  // Multiply by 1000 to go from big blinds to milli-big-blinds
  double mbb_g = ((mean_pair_outcome / 2.0) / 2.0) * 1000.0;
  printf("Avg A outcome: %f (%.1f mbb/g) over %llu dup hands\n",
	 mean_pair_outcome / 2.0, mbb_g, num_duplicate_hands);
  // Variance is the mean of the squares minus the square of the means
  double var_pair =
    (((double)sum_sqd_pair_outcomes) / ((double)num_duplicate_hands)) -
    (mean_pair_outcome * mean_pair_outcome);
  double stddev_pair = sqrt(var_pair);
  double match_stddev = stddev_pair * sqrt(num_duplicate_hands);
  double match_lower = sum_pair_outcomes - 1.96 * match_stddev;
  double match_upper = sum_pair_outcomes + 1.96 * match_stddev;
  double mbb_lower =
    ((match_lower / (2.0 * num_duplicate_hands)) / 2.0) * 1000.0;
  double mbb_upper =
    ((match_upper / (2.0 * num_duplicate_hands)) / 2.0) * 1000.0;
  printf("MBB confidence interval: %f-%f\n", mbb_lower, mbb_upper);
  fflush(stdout);

  double avg_p1_outcome =
    ((double)sum_p1_outcomes_) / (double)(2 * num_duplicate_hands);
  printf("Avg P1 outcome: %f\n", avg_p1_outcome);
  fflush(stdout);
}

Player::Player(BettingTree *base_betting_tree,
	       BettingTree *merged_betting_tree,
	       const BettingAbstraction &base_ba,
	       const BettingAbstraction &merged_ba,
	       const CFRConfig &a_cc, const CFRConfig &b_cc,
	       const RuntimeConfig &a_rc, const RuntimeConfig &b_rc) :
  base_ba_(base_ba), merged_ba_(merged_ba), a_cc_(a_cc), b_cc_(b_cc) {
  base_betting_tree_ = base_betting_tree;
  merged_betting_tree_ = merged_betting_tree;

  BoardTree::Create();
  BoardTree::CreateLookup();
  unsigned int max_street = Game::MaxStreet();
  bool *trunk_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    trunk_streets[st] = st < resolve_st_;
  }
  a_trunk_probs_.reset(new CFRValues(true, true, true, trunk_streets,
				     merged_betting_tree_->NumNonterminals(),
				     0, 0, kMaxUInt));
  delete [] trunk_streets;

  unsigned int a_it = a_rc.Iteration();
  char dir[500];
  sprintf(dir, "%s/%s.null.%u.%u.%u.%s.%s",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_ba_.BettingAbstractionName().c_str(),
	  a_cc_.CFRConfigName().c_str());
  a_trunk_probs_->Read(dir, a_it, base_betting_tree_->Root(), 0, kMaxUInt);

  b_probs_.reset(new CFRValues(true, true, true, nullptr,
			       merged_betting_tree_->NumNonterminals(),
			       0, 0, b_cc_.SplitStreet()));
  unsigned int b_it = b_rc.Iteration();
  sprintf(dir, "%s/%s.null.%u.%u.%u.%s.%s",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_ba_.BettingAbstractionName().c_str(),
	  b_cc_.CFRConfigName().c_str());
  b_probs_->Read(dir, b_it, merged_betting_tree->Root(), 0, kMaxUInt);

  boards_ = new unsigned int[max_street + 1];
  boards_[0] = 0;
  p1_hcps_ = new unsigned int[max_street + 1];
  p2_hcps_ = new unsigned int[max_street + 1];

  HandTree hand_tree(0, 0, max_street);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(max_street);
  unsigned int num_boards = BoardTree::NumBoards(max_street);
  sorted_hcps_ = new unsigned int *[num_boards];
  Card cards[7];
  unsigned int num_hole_cards = Game::NumCardsForStreet(0);
  unsigned int num_board_cards = Game::NumBoardCards(max_street);
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    const Card *board = BoardTree::Board(max_street, bd);
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      cards[i + num_hole_cards] = board[i];
    }
    sorted_hcps_[bd] = new unsigned int[num_hole_card_pairs];
    const CanonicalCards *hands = hand_tree.Hands(max_street, bd);
    for (unsigned int shcp = 0; shcp < num_hole_card_pairs; ++shcp) {
      const Card *hole_cards = hands->Cards(shcp);
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	cards[i] = hole_cards[i];
      }
      unsigned int rhcp = HCPIndex(max_street, cards);
      sorted_hcps_[bd][rhcp] = shcp;
    }
  }

  if (resolve_st_ <= max_street) {
    endgame_solver_ = new EndgameSolver(resolve_st_, *a_trunk_probs_,
					a_rc.Iteration(), base_ba_,
					merged_ba_, a_cc_,
					ResolvingMethod::CFRD, 1);
  } else {
    endgame_solver_ = NULL;
  }

  sum_p1_outcomes_ = 0LL;
}

Player::~Player(void) {
  delete endgame_solver_;
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_boards = BoardTree::NumBoards(max_street);
  for (unsigned int bd = 0; bd < num_boards; ++bd) {
    delete [] sorted_hcps_[bd];
  }
  delete [] sorted_hcps_;
  delete [] boards_;
  delete [] p1_hcps_;
  delete [] p2_hcps_;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base betting abstraction params> "
	  "<merged betting abstraction params> "
	  "<A CFR params> <B CFR params> <A runtime params> "
	  "<B runtime params> <A it> <B it> <resolve st> <num duplicate hands> "
	  "[determ|nondeterm]\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 13) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> base_betting_params = CreateBettingAbstractionParams();
  base_betting_params->ReadFromFile(argv[2]);
  unique_ptr<BettingAbstraction>
    base_betting_abstraction(new BettingAbstraction(*base_betting_params));
  unique_ptr<Params> merged_betting_params = CreateBettingAbstractionParams();
  merged_betting_params->ReadFromFile(argv[3]);
  unique_ptr<BettingAbstraction> merged_betting_abstraction(
		      new BettingAbstraction(*merged_betting_params));
  unique_ptr<Params> a_cfr_params = CreateCFRParams();
  a_cfr_params->ReadFromFile(argv[4]);
  unique_ptr<CFRConfig>
    a_cfr_config(new CFRConfig(*a_cfr_params));
  unique_ptr<Params> b_cfr_params = CreateCFRParams();
  b_cfr_params->ReadFromFile(argv[5]);
  unique_ptr<CFRConfig>
    b_cfr_config(new CFRConfig(*b_cfr_params));
  unique_ptr<Params> a_runtime_params = CreateRuntimeParams();
  a_runtime_params->ReadFromFile(argv[6]);
  unique_ptr<RuntimeConfig>
    a_runtime_config(new RuntimeConfig(*a_runtime_params));
  unique_ptr<Params> b_runtime_params = CreateRuntimeParams();
  b_runtime_params->ReadFromFile(argv[7]);
  unique_ptr<RuntimeConfig>
    b_runtime_config(new RuntimeConfig(*b_runtime_params));

  unsigned int a_it, b_it, resolve_st;
  if (sscanf(argv[8], "%u", &a_it) != 1) Usage(argv[0]);
  if (sscanf(argv[9], "%u", &b_it) != 1) Usage(argv[0]);
  if (sscanf(argv[10], "%u", &resolve_st) != 1) Usage(argv[0]);
  unsigned long long int num_duplicate_hands;
  if (sscanf(argv[11], "%llu", &num_duplicate_hands) != 1) Usage(argv[0]);
  string darg = argv[12];
  bool deterministic;
  if (darg == "determ")         deterministic = true;
  else if (darg == "nondeterm") deterministic = false;
  else                          Usage(argv[0]);

  a_runtime_config->SetIteration(a_it);
  b_runtime_config->SetIteration(b_it);
  fprintf(stderr, "A iteration %llu\n", a_runtime_config->Iteration());
  fprintf(stderr, "B iteration %llu\n", b_runtime_config->Iteration());

  HandValueTree::Create();

  // Leave this in if we don't want reproducibility
  InitRand();
  BettingTree *base_betting_tree =
    BettingTree::BuildTree(*base_betting_abstraction);
  BettingTree *merged_betting_tree =
    BettingTree::BuildTree(*merged_betting_abstraction);

  Player *player = new Player(base_betting_tree, merged_betting_tree,
			      *base_betting_abstraction,
			      *merged_betting_abstraction,
			      *a_cfr_config, *b_cfr_config, *a_runtime_config,
			      *b_runtime_config);
  player->Go(num_duplicate_hands, deterministic);
  delete player;
  delete base_betting_tree;
  delete merged_betting_tree;
}
