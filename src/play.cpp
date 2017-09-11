// There are two players (computed strategies) given to the program, named
// A and B.  P0 and P1 in contrast refer to the two positions (big blind and
// button respectively).  A and B alternate during play between being the
// button (P1) and the big blind (P0).

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
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "params.h"
#include "rand.h"
#include "sorting.h"

using namespace std;

class Player {
public:
  Player(BettingTree *betting_tree, const BettingAbstraction &ba,
	 const CardAbstraction &a_ca, const CardAbstraction &b_ca,
	 const Buckets &a_buckets, const Buckets &b_buckets,
	 const CFRConfig &a_cc, const CFRConfig &b_cc,
	 unsigned int a_it, unsigned int b_it);
  ~Player(void);
  void Go(unsigned long long int num_duplicate_hands, bool deterministic);
private:
  void SetHCPsAndBoards(const Card *p0_hand_cards, const Card *p1_hand_cards);
  int Play(Node *node, bool a_is_p1);
  int PlayDuplicateHand(unsigned int h, const Card *cards,
			bool deterministic);

  BettingTree *betting_tree_;
  const Buckets &a_buckets_;
  const Buckets &b_buckets_;
  CFRValues *a_probs_;
  CFRValues *b_probs_;
  unsigned int *boards_;
  unsigned int **raw_hcps_;
  int p1_showdown_;
  unsigned short **sorted_hcps_;
  long long int sum_p1_outcomes_;
};

// Returns outcomes from P1's perspective
int Player::Play(Node *node, bool a_is_p1) {
  if (node->Terminal()) {
    if (node->PlayerFolding() == 1) {
      int ret = -((int)node->PotSize()) / 2;
      return ret;
    } else if (node->PlayerFolding() == 0) {
      int ret = ((int)node->PotSize()) / 2;
      return ret;
    } else {
      // Showdown
      int ret = p1_showdown_ * ((int)node->PotSize() / 2);
      return ret;
    }
  } else {
    unsigned int nt = node->NonterminalID();
    unsigned int st = node->Street();
    unsigned int num_succs = node->NumSuccs();
    unsigned int pa = node->PlayerActing();
    unsigned int dsi = node->DefaultSuccIndex();
    unsigned int bd = boards_[st];
    unsigned int raw_hcp = raw_hcps_[pa][st];
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int a_offset, b_offset;
    // If card abstraction, hcp on river should be raw.  If no card
    // abstraction, hcp on river should be sorted.  Right?
    if (a_buckets_.None(st)) {
      unsigned int hcp = st == Game::MaxStreet() ? sorted_hcps_[bd][raw_hcp] :
	raw_hcp;
      a_offset = bd * num_hole_card_pairs * num_succs + hcp * num_succs;
    } else {
      unsigned int h = bd * num_hole_card_pairs + raw_hcp;
      unsigned int b = a_buckets_.Bucket(st, h);
      a_offset = b * num_succs;
    }
    if (b_buckets_.None(st)) {
      unsigned int hcp = st == Game::MaxStreet() ? sorted_hcps_[bd][raw_hcp] :
	raw_hcp;
      b_offset = bd * num_hole_card_pairs * num_succs + hcp * num_succs;
    } else {
      unsigned int h = bd * num_hole_card_pairs + raw_hcp;
      unsigned int b = b_buckets_.Bucket(st, h);
      b_offset = b * num_succs;
    }
    double r = RandZeroToOne();
    double cum = 0;
    int s;
    for (s = 0; s < ((int)num_succs) - 1; ++s) {
      double prob;
      if (a_is_p1 == (pa == 1)) {
	prob = a_probs_->Prob(pa, st, nt, a_offset, s, num_succs, dsi);
      } else {
	prob = b_probs_->Prob(pa, st, nt, b_offset, s, num_succs, dsi);
      }
      cum += prob;
      if (r < cum) break;
    }
    int ret = Play(node->IthSucc(s), a_is_p1);
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
    int p1_outcome = Play(betting_tree_->Root(), a_is_p1);
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

void Player::SetHCPsAndBoards(const Card *p0_hand_cards,
			      const Card *p1_hand_cards) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (st == 0) {
      raw_hcps_[0][0] = HCPIndex(st, p0_hand_cards);
      raw_hcps_[1][0] = HCPIndex(st, p1_hand_cards);
    } else {
      // Store the hole cards *after* the board cards
      unsigned int num_hole_cards = Game::NumCardsForStreet(0);
      Card p0_raw_cards[7], p0_canon_cards[7];
      Card p1_raw_cards[7], p1_canon_cards[7];
      unsigned int num_board_cards = Game::NumBoardCards(st);
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	p0_raw_cards[i] = p0_hand_cards[i + num_hole_cards];
	p1_raw_cards[i] = p1_hand_cards[i + num_hole_cards];
      }
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	p0_raw_cards[num_board_cards + i] = p0_hand_cards[i];
	p1_raw_cards[num_board_cards + i] = p1_hand_cards[i];
      }
      bool p0_change_made = 
	CanonicalCards::ToCanon2(p0_raw_cards, num_board_cards + num_hole_cards,
				 0, p0_canon_cards);
      if (p0_change_made) {
	num_board_cards = 0;
	for (unsigned int st1 = 1; st1 <= st; ++st1) {
	  unsigned int num_street_cards = Game::NumCardsForStreet(st1);
	  SortCards(p0_canon_cards + num_board_cards, num_street_cards);
	  num_board_cards += num_street_cards;
	}
	SortCards(p0_canon_cards + num_board_cards, num_hole_cards);
      }
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
      unsigned int bd = BoardTree::LookupBoard(p1_canon_cards, st);
      boards_[st] = bd;
      // Put the hole cards back at the beginning
      Card canon_cards2[7];
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	canon_cards2[num_hole_cards + i] = p1_canon_cards[i];
      }
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	canon_cards2[i] = p0_canon_cards[num_board_cards + i];
      }
      raw_hcps_[0][st] = HCPIndex(st, canon_cards2);
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	canon_cards2[i] = p1_canon_cards[num_board_cards + i];
      }
      raw_hcps_[1][st] = HCPIndex(st, canon_cards2);
    }
  }
#if 0
  // On the final street we need the sorted HCP index (the index into the
  // vector of hole card pairs that has been sorted by hand strength).
  unsigned int msbd = boards_[max_street];
  p0_hcps_[max_street] = sorted_hcps_[msbd][p0_hcps_[max_street]];
  p1_hcps_[max_street] = sorted_hcps_[msbd][p1_hcps_[max_street]];
#endif
}

void Player::Go(unsigned long long int num_duplicate_hands,
		bool deterministic) {
  // From perspective of player A
  long long int sum_pair_outcomes = 0;
  long long int sum_sqd_pair_outcomes = 0;
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_board_cards = Game::NumBoardCards(max_street);
  Card cards[9], p0_hand_cards[7], p1_hand_cards[7];
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
      p0_hand_cards[i + 2] = cards[i + 4];
      p1_hand_cards[i + 2] = cards[i + 4];
    }
    p0_hand_cards[0] = cards[0];
    p0_hand_cards[1] = cards[1];
    unsigned int p0_hv = HandValueTree::Val(p0_hand_cards);
    p1_hand_cards[0] = cards[2];
    p1_hand_cards[1] = cards[3];
    unsigned int p1_hv = HandValueTree::Val(p1_hand_cards);
    SetHCPsAndBoards(p0_hand_cards, p1_hand_cards);
    if (p1_hv > p0_hv)      p1_showdown_ = 1;
    else if (p0_hv > p1_hv) p1_showdown_ = -1;
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

Player::Player(BettingTree *betting_tree, const BettingAbstraction &ba,
	       const CardAbstraction &a_ca, const CardAbstraction &b_ca,
	       const Buckets &a_buckets, const Buckets &b_buckets,
	       const CFRConfig &a_cc, const CFRConfig &b_cc,
	       unsigned int a_it, unsigned int b_it) :
  a_buckets_(a_buckets), b_buckets_(b_buckets) {
  betting_tree_ = betting_tree;
  BoardTree::Create();
  BoardTree::CreateLookup();
  unsigned int max_street = Game::MaxStreet();
  a_probs_ = new CFRValues(nullptr, true, nullptr, betting_tree, 0, 0,
			   a_ca, a_buckets, nullptr);
  b_probs_ = new CFRValues(nullptr, true, nullptr, betting_tree, 0, 0,
			   b_ca, b_buckets, nullptr);

  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), a_ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  ba.BettingAbstractionName().c_str(),
	  a_cc.CFRConfigName().c_str());
  a_probs_->Read(dir, a_it, betting_tree->Root(),
		 betting_tree->Root()->NonterminalID(), kMaxUInt);
  fprintf(stderr, "Read A probs\n");
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), b_ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  ba.BettingAbstractionName().c_str(),
	  b_cc.CFRConfigName().c_str());
  b_probs_->Read(dir, b_it, betting_tree->Root(),
		 betting_tree->Root()->NonterminalID(), kMaxUInt);
  fprintf(stderr, "Read B probs\n");

  boards_ = new unsigned int[max_street + 1];
  boards_[0] = 0;
  raw_hcps_ = new unsigned int *[2];
  for (unsigned int p = 0; p <= 1; ++p) {
    raw_hcps_[p] = new unsigned int[max_street + 1];
  }

  if (a_buckets_.None(max_street) || b_buckets_.None(max_street)) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(max_street);
    unsigned int num_boards = BoardTree::NumBoards(max_street);
    sorted_hcps_ = new unsigned short *[num_boards];
    Card cards[7];
    unsigned int num_hole_cards = Game::NumCardsForStreet(0);
    unsigned int num_board_cards = Game::NumBoardCards(max_street);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      if (bd % 100000 == 0) {
	fprintf(stderr, "bd %u\n", bd);
      }
      const Card *board = BoardTree::Board(max_street, bd);
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	cards[i + num_hole_cards] = board[i];
      }
      unsigned int sg = BoardTree::SuitGroups(max_street, bd);
      CanonicalCards hands(2, board, num_board_cards, sg, false);
      hands.SortByHandStrength(board);
      sorted_hcps_[bd] = new unsigned short[num_hole_card_pairs];
      for (unsigned int shcp = 0; shcp < num_hole_card_pairs; ++shcp) {
	const Card *hole_cards = hands.Cards(shcp);
	for (unsigned int i = 0; i < num_hole_cards; ++i) {
	  cards[i] = hole_cards[i];
	}
	unsigned int rhcp = HCPIndex(max_street, cards);
	sorted_hcps_[bd][rhcp] = shcp;
      }
    }
    fprintf(stderr, "Created sorted_hcps_\n");
  } else {
    sorted_hcps_ = nullptr;
    fprintf(stderr, "Not creating sorted_hcps_\n");
  }

  sum_p1_outcomes_ = 0LL;
}

Player::~Player(void) {
  if (sorted_hcps_) {
    unsigned int max_street = Game::MaxStreet();
    unsigned int num_boards = BoardTree::NumBoards(max_street);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      delete [] sorted_hcps_[bd];
    }
    delete [] sorted_hcps_;
  }
  delete [] boards_;
  for (unsigned int p = 0; p <= 1; ++p) {
    delete [] raw_hcps_[p];
  }
  delete [] raw_hcps_;
  delete a_probs_;
  delete b_probs_;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <A card params> <B card params> "
	  "<betting abstraction params> <A CFR params> <B CFR params> "
	  "<A it> <B it> <num duplicate hands> "
	  "[determ|nondeterm]\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 11) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> a_card_params = CreateCardAbstractionParams();
  a_card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    a_card_abstraction(new CardAbstraction(*a_card_params));
  unique_ptr<Params> b_card_params = CreateCardAbstractionParams();
  b_card_params->ReadFromFile(argv[3]);
  unique_ptr<CardAbstraction>
    b_card_abstraction(new CardAbstraction(*b_card_params));
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unique_ptr<Params> a_cfr_params = CreateCFRParams();
  a_cfr_params->ReadFromFile(argv[5]);
  unique_ptr<CFRConfig>
    a_cfr_config(new CFRConfig(*a_cfr_params));
  unique_ptr<Params> b_cfr_params = CreateCFRParams();
  b_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig>
    b_cfr_config(new CFRConfig(*b_cfr_params));

  unsigned int a_it, b_it;
  if (sscanf(argv[7], "%u", &a_it) != 1) Usage(argv[0]);
  if (sscanf(argv[8], "%u", &b_it) != 1) Usage(argv[0]);
  unsigned long long int num_duplicate_hands;
  if (sscanf(argv[9], "%llu", &num_duplicate_hands) != 1) Usage(argv[0]);
  string darg = argv[10];
  bool deterministic;
  if (darg == "determ")         deterministic = true;
  else if (darg == "nondeterm") deterministic = false;
  else                          Usage(argv[0]);

  HandValueTree::Create();
  fprintf(stderr, "Created HandValueTree\n");

  // Leave this in if we don't want reproducibility
  InitRand();
  BettingTree *betting_tree = BettingTree::BuildTree(*betting_abstraction);
  Buckets *a_buckets, *b_buckets;
  a_buckets = new Buckets(*a_card_abstraction, false);
  fprintf(stderr, "Created a_buckets\n");
  if (strcmp(argv[2], argv[3])) {
    b_buckets = new Buckets(*b_card_abstraction, false);
    fprintf(stderr, "Created b_buckets\n");
  } else {
    b_buckets = a_buckets;
  }

  Player *player = new Player(betting_tree, *betting_abstraction,
			      *a_card_abstraction, *b_card_abstraction,
			      *a_buckets, *b_buckets, *a_cfr_config,
			      *b_cfr_config, a_it, b_it);
  player->Go(num_duplicate_hands, deterministic);
  delete player;
  delete a_buckets;
  if (strcmp(argv[2], argv[3])) delete b_buckets;
  delete betting_tree;
}
