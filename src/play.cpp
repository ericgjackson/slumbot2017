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
#include "canonical.h"
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
	 const CFRConfig &a_cc, const CFRConfig &b_cc,
	 unsigned int a_it, unsigned int b_it, bool mem_buckets);
  ~Player(void);
  void Go(unsigned long long int num_duplicate_hands, bool deterministic);
private:
  void DealNCards(Card *cards, unsigned int n);
  void SetHCPsAndBoards(Card **raw_hole_cards, const Card *raw_board);
  void Play(Node *node, unsigned int b_pos, unsigned int *contributions,
	    unsigned int last_bet_to, bool *folded, unsigned int num_remaining,
	    unsigned int last_player_acting, int last_st, double *outcomes);
  void PlayDuplicateHand(unsigned long long int h, const Card *cards,
			 bool deterministic, double *a_sum, double *b_sum);

  bool mem_buckets_;
  unsigned int num_players_;
  BettingTree *betting_tree_;
  const Buckets *a_buckets_;
  const Buckets *b_buckets_;
  const BucketsFile *a_buckets_file_;
  const BucketsFile *b_buckets_file_;
  CFRValues *a_probs_;
  CFRValues *b_probs_;
  unsigned int *boards_;
  unsigned int **raw_hcps_;
  unique_ptr<unsigned int []> hvs_;
  unique_ptr<bool []> winners_;
  unsigned short **sorted_hcps_;
  unique_ptr<double []> sum_pos_outcomes_;
  struct drand48_data *rand_bufs_;
};

void Player::Play(Node *node, unsigned int b_pos, unsigned int *contributions,
		  unsigned int last_bet_to, bool *folded,
		  unsigned int num_remaining, unsigned int last_player_acting,
		  int last_st, double *outcomes) {
  if (node->Terminal()) {
    if (num_remaining == 1) {
      unsigned int sum_other_contributions = 0;
      unsigned int remaining_p = kMaxUInt;
      for (unsigned int p = 0; p < num_players_; ++p) {
	if (folded[p]) {
	  sum_other_contributions += contributions[p];
	} else {
	  remaining_p = p;
	}
      }
      outcomes[remaining_p] = sum_other_contributions;
    } else {
      // Showdown
      // Temporary?
      if (num_players_ == 2 &&
	  (contributions[0] != contributions[1] ||
	   contributions[0] != node->LastBetTo())) {
	fprintf(stderr, "Mismatch %u %u %u\n", contributions[0],
		contributions[1], node->LastBetTo());
	fprintf(stderr, "TID: %u\n", node->TerminalID());
	exit(-1);
      }

      // Find the best hand value of anyone remaining in the hand, and the
      // total pot size which includes contributions from remaining players
      // and players who folded earlier.
      unsigned int best_hv = 0;
      unsigned int pot_size = 0;
      for (unsigned int p = 0; p < num_players_; ++p) {
	pot_size += contributions[p];
	if (! folded[p]) {
	  unsigned int hv = hvs_[p];
	  if (hv > best_hv) best_hv = hv;
	}
      }

      // Determine if we won, the number of winners, and the total contribution
      // of all winners.
      unsigned int num_winners = 0;
      unsigned int winner_contributions = 0;
      for (unsigned int p = 0; p < num_players_; ++p) {
	if (! folded[p] && hvs_[p] == best_hv) {
	  winners_[p] = true;
	  ++num_winners;
	  winner_contributions += contributions[p];
	} else {
	  winners_[p] = false;
	}
      }
      
      for (unsigned int p = 0; p < num_players_; ++p) {
	if (winners_[p]) {
	  outcomes[p] = ((double)(pot_size - winner_contributions)) /
	    ((double)num_winners);
	} else if (! folded[p]) {
	  outcomes[p] = -(int)contributions[p];
	}
      }
    }
    return;
  } else {
    unsigned int nt = node->NonterminalID();
    unsigned int st = node->Street();
    unsigned int num_succs = node->NumSuccs();
    unsigned int orig_pa = node->PlayerActing();
    // Find the next player to act.  Start with the first candidate and move
    // forward until we find someone who has not folded.  The first candidate
    // is either the last player plus one, or, if we are starting a new
    // betting round, the first player to act on that street.
    unsigned int actual_pa;
    if ((int)st > last_st) {
      actual_pa = Game::FirstToAct(st);
    } else {
      actual_pa = last_player_acting + 1;
    }
    while (true) {
      if (actual_pa == num_players_) actual_pa = 0;
      if (! folded[actual_pa]) break;
      ++actual_pa;
    }
    
    unsigned int dsi = node->DefaultSuccIndex();
    unsigned int bd = boards_[st];
    unsigned int raw_hcp = raw_hcps_[actual_pa][st];
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int a_offset, b_offset;
    // If card abstraction, hcp on river should be raw.  If no card
    // abstraction, hcp on river should be sorted.  Right?
    if (a_buckets_->None(st)) {
      unsigned int hcp = st == Game::MaxStreet() ? sorted_hcps_[bd][raw_hcp] :
	raw_hcp;
      a_offset = bd * num_hole_card_pairs * num_succs + hcp * num_succs;
    } else {
      unsigned int h = bd * num_hole_card_pairs + raw_hcp;
      unsigned int b;
      if (mem_buckets_) {
	b = a_buckets_->Bucket(st, h);
      } else {
	b = a_buckets_file_->Bucket(st, h);
      }
      a_offset = b * num_succs;
    }
    unsigned int hcp;
    if (b_buckets_->None(st)) {
      hcp = st == Game::MaxStreet() ? sorted_hcps_[bd][raw_hcp] : raw_hcp;
      b_offset = bd * num_hole_card_pairs * num_succs + hcp * num_succs;
    } else {
      unsigned int h = bd * num_hole_card_pairs + raw_hcp;
      unsigned int b;
      if (mem_buckets_) {
	b = b_buckets_->Bucket(st, h);
      } else {
	b = b_buckets_file_->Bucket(st, h);
      }
      b_offset = b * num_succs;
    }
    double r;
    // r = RandZeroToOne();
    drand48_r(&rand_bufs_[actual_pa], &r);
    
    double cum = 0;
    unique_ptr<double []> probs(new double[num_succs]);
    if (actual_pa == b_pos) {
      b_probs_->Probs(orig_pa, st, nt, b_offset, num_succs, dsi, probs.get());
#if 0
      fprintf(stderr, "b pa %u st %u nt %u bd %u nhcp %u hcp %u b_offset %u "
	      "probs %f %f r %f\n", orig_pa, st, nt, bd, num_hole_card_pairs,
	      hcp, b_offset, probs[0], probs[1], r);
#endif
    } else {
      a_probs_->Probs(orig_pa, st, nt, a_offset, num_succs, dsi, probs.get());
#if 0
      fprintf(stderr, "a pa %u st %u nt %u bd %u nhcp %u hcp %u b_offset %u "
	      "probs %f %f r %f\n", orig_pa, st, nt, bd, num_hole_card_pairs,
	      hcp, a_offset, probs[0], probs[1], r);
#endif
    }
    int s;
    for (s = 0; s < ((int)num_succs) - 1; ++s) {
      // Look up probabilities with orig_pa which may be different from
      // actual_pa.
      double prob = probs[s];
      cum += prob;
      if (r < cum) break;
    }
    if (s == (int)node->CallSuccIndex()) {
      // fprintf(stderr, "Call\n");
      contributions[actual_pa] = last_bet_to;
      Play(node->IthSucc(s), b_pos, contributions, last_bet_to, folded,
	   num_remaining, actual_pa, st, outcomes);
    } else if (s == (int)node->FoldSuccIndex()) {
      // fprintf(stderr, "Fold\n");
      folded[actual_pa] = true;
      outcomes[actual_pa] = -(int)contributions[actual_pa];
      Play(node->IthSucc(s), b_pos, contributions, last_bet_to, folded,
	   num_remaining - 1, actual_pa, st, outcomes);
    } else {
      Node *succ = node->IthSucc(s);
      // fprintf(stderr, "Bet\n");
      unsigned int new_bet_to = succ->LastBetTo();
      contributions[actual_pa] = new_bet_to;
      Play(succ, b_pos, contributions, new_bet_to, folded, num_remaining,
	   actual_pa, st, outcomes);
    }
  }
}

static unsigned int PrecedingPlayer(unsigned int p) {
  if (p == 0) return Game::NumPlayers() - 1;
  else        return p - 1;
}

// Play one hand of duplicate, which is a pair of regular hands.  Return
// outcome from A's perspective.
void Player::PlayDuplicateHand(unsigned long long int h, const Card *cards,
			       bool deterministic, double *a_sum,
			       double *b_sum) {
  unique_ptr<double []> outcomes(new double[num_players_]);
  unique_ptr<unsigned int []> contributions(new unsigned int[num_players_]);
  unique_ptr<bool []> folded(new bool[num_players_]);
  // Assume the big blind is last to act preflop
  // Assume the small blind is prior to the big blind
  unsigned int big_blind_p = PrecedingPlayer(Game::FirstToAct(0));
  unsigned int small_blind_p = PrecedingPlayer(big_blind_p);
  *a_sum = 0;
  *b_sum = 0;
  for (unsigned int b_pos = 0; b_pos < num_players_; ++b_pos) {
    if (deterministic) {
      // Reseed the RNG again before play within this loop.  This ensure
      // that if we play a system against itself, the duplicate outcome will
      // always be zero.
      //
      // This has a big impact on the average P1 outcome - why?  Too much
      // coordination between the RNG for dealing the cards and the RNG for
      // actions?
      //
      // Temporary - to match play_agents
      // SeedRand(h);
      // Generate a separate seed for each player that depends on the hand
      // index.
      for (unsigned int p = 0; p < num_players_; ++p) {
	srand48_r(h * num_players_ + p, &rand_bufs_[p]);
      }
    }
    
    for (unsigned int p = 0; p < num_players_; ++p) {
      folded[p] = false;
      if (p == small_blind_p) {
	contributions[p] = Game::SmallBlind();
      } else if (p == big_blind_p) {
	contributions[p] = Game::BigBlind();
      } else {
	contributions[p] = 0;
      }
    }
    Play(betting_tree_->Root(), b_pos, contributions.get(), Game::BigBlind(),
	 folded.get(), num_players_, 1000, -1, outcomes.get());
    for (unsigned int p = 0; p < num_players_; ++p) {
      if (p == b_pos) {
	*b_sum += outcomes[p];
      } else {
	*a_sum += outcomes[p];
      }
      sum_pos_outcomes_[p] += outcomes[p];
    }
  }
}

void Player::DealNCards(Card *cards, unsigned int n) {
  unsigned int max_card = Game::MaxCard();
  for (unsigned int i = 0; i < n; ++i) {
    Card c;
    while (true) {
      // c = RandBetween(0, max_card);
      double r;
      drand48_r(&rand_bufs_[0], &r);
      c = (max_card + 1) * r;
      unsigned int j;
      for (j = 0; j < i; ++j) {
	if (cards[j] == c) break;
      }
      if (j == i) break;
    }
    cards[i] = c;
  }
}

void Player::SetHCPsAndBoards(Card **raw_hole_cards, const Card *raw_board) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (st == 0) {
      for (unsigned int p = 0; p < num_players_; ++p) {
	raw_hcps_[p][0] = HCPIndex(st, raw_hole_cards[p]);
      }
    } else {
      // Store the hole cards *after* the board cards
      unsigned int num_hole_cards = Game::NumCardsForStreet(0);
      unsigned int num_board_cards = Game::NumBoardCards(st);
      for (unsigned int p = 0; p < num_players_; ++p) {
	Card canon_board[5];
	Card canon_hole_cards[2];
	CanonicalizeCards(raw_board, raw_hole_cards[p], st,
			  canon_board, canon_hole_cards);
	// Don't need to do this repeatedly
	if (p == 0) {
	  boards_[st] = BoardTree::LookupBoard(canon_board, st);
	}
	Card canon_cards[7];
	for (unsigned int i = 0; i < num_board_cards; ++i) {
	  canon_cards[num_hole_cards + i] = canon_board[i];
	}
	for (unsigned int i = 0; i < num_hole_cards; ++i) {
	  canon_cards[i] = canon_hole_cards[i];
	}
	raw_hcps_[p][st] = HCPIndex(st, canon_cards);
      }
    }
  }
}

void Player::Go(unsigned long long int num_duplicate_hands,
		bool deterministic) {
  double sum_a_outcomes = 0, sum_b_outcomes = 0;
  double sum_sqd_a_outcomes = 0, sum_sqd_b_outcomes = 0;
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_board_cards = Game::NumBoardCards(max_street);
  Card cards[100], hand_cards[7];
  Card **hole_cards = new Card *[num_players_];
  for (unsigned int p = 0; p < num_players_; ++p) {
    hole_cards[p] = new Card[2];
  }
  if (! deterministic) {
    InitRand();
  }
  for (unsigned long long int h = 0; h < num_duplicate_hands; ++h) {
    if (deterministic) {
      // Seed just as we do in play_agents so we can get the same cards and
      // compare results.
      // SeedRand(h);
      srand48_r(h, &rand_bufs_[0]);
    }
    // Assume 2 hole cards
    DealNCards(cards, num_board_cards + 2 * num_players_);
#if 0
    OutputNCards(cards + 2 * num_players_, num_board_cards);
    printf("\n");
    OutputTwoCards(cards);
    printf("\n");
    OutputTwoCards(cards + 2);
    printf("\n");
    fflush(stdout);
#endif
    for (unsigned int p = 0; p < num_players_; ++p) {
      SortCards(cards + 2 * p, 2);
    }
    unsigned int num = 2 * num_players_;
    for (unsigned int st = 1; st <= max_street; ++st) {
      unsigned int num_street_cards = Game::NumCardsForStreet(st);
      SortCards(cards + num, num_street_cards);
      num += num_street_cards;
    }
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      hand_cards[i+2] = cards[i + 2 * num_players_];
    }
    for (unsigned int p = 0; p < num_players_; ++p) {
      hand_cards[0] = cards[2 * p];
      hand_cards[1] = cards[2 * p + 1];
      hvs_[p] = HandValueTree::Val(hand_cards);
      hole_cards[p][0] = cards[2 * p];
      hole_cards[p][1] = cards[2 * p + 1];
    }
    
    SetHCPsAndBoards(hole_cards, cards + 2 * num_players_);

    // PlayDuplicateHand() returns the result of a duplicate hand (which is
    // N hands if N is the number of players)
    double a_outcome, b_outcome;
    PlayDuplicateHand(h, cards, deterministic, &a_outcome, &b_outcome);
    sum_a_outcomes += a_outcome;
    sum_b_outcomes += b_outcome;
    sum_sqd_a_outcomes += a_outcome * a_outcome;
    sum_sqd_b_outcomes += b_outcome * b_outcome;
  }
  for (unsigned int p = 0; p < num_players_; ++p) {
    delete [] hole_cards[p];
  }
  delete [] hole_cards;
#if 0
  unsigned long long int num_a_hands =
    (num_players_ - 1) * num_players_ * num_duplicate_hands;
  double mean_a_outcome = sum_a_outcomes / (double)num_a_hands;
#endif
  // Divide by num_players because we evaluate B that many times (once for
  // each position).
  unsigned long long int num_b_hands = num_duplicate_hands * num_players_;
  double mean_b_outcome = sum_b_outcomes / (double)num_b_hands;
  // Need to divide by two to convert from small blind units to big blind units
  // Multiply by 1000 to go from big blinds to milli-big-blinds
  double b_mbb_g = (mean_b_outcome / 2.0) * 1000.0;
  printf("Avg B outcome: %f (%.1f mbb/g) over %llu dup hands\n",
	 mean_b_outcome, b_mbb_g, num_duplicate_hands);
  // Variance is the mean of the squares minus the square of the means
  double var_b =
    (sum_sqd_b_outcomes / ((double)num_b_hands)) -
    (mean_b_outcome * mean_b_outcome);
  double stddev_b = sqrt(var_b);
  double match_stddev = stddev_b * sqrt(num_b_hands);
  double match_lower = sum_b_outcomes - 1.96 * match_stddev;
  double match_upper = sum_b_outcomes + 1.96 * match_stddev;
  double mbb_lower =
    ((match_lower / (num_b_hands)) / 2.0) * 1000.0;
  double mbb_upper =
    ((match_upper / (num_b_hands)) / 2.0) * 1000.0;
  printf("MBB confidence interval: %f-%f\n", mbb_lower, mbb_upper);
  fflush(stdout);

  for (unsigned int p = 0; p < num_players_; ++p) {
    double avg_outcome =
      sum_pos_outcomes_[p] / (double)(num_players_ * num_duplicate_hands);
    printf("Avg P%u outcome: %f\n", p, avg_outcome);
    fflush(stdout);
  }
}

Player::Player(BettingTree *betting_tree, const BettingAbstraction &ba,
	       const CardAbstraction &a_ca, const CardAbstraction &b_ca,
	       const CFRConfig &a_cc, const CFRConfig &b_cc,
	       unsigned int a_it, unsigned int b_it, bool mem_buckets) {
  mem_buckets_ = mem_buckets;
  if (mem_buckets_) {
    a_buckets_ = new Buckets(a_ca, false);
    fprintf(stderr, "Created a_buckets\n");
    if (strcmp(a_ca.CardAbstractionName().c_str(),
	       b_ca.CardAbstractionName().c_str())) {
      b_buckets_ = new Buckets(b_ca, false);
      fprintf(stderr, "Created b_buckets\n");
    } else {
      b_buckets_ = a_buckets_;
    }
    a_buckets_file_ = nullptr;
    b_buckets_file_ = nullptr;
  } else {
    a_buckets_ = new Buckets(a_ca, true);
    b_buckets_ = new Buckets(b_ca, true);
    a_buckets_file_ = new BucketsFile(a_ca);
    b_buckets_file_ = new BucketsFile(b_ca);
  }
  num_players_ = Game::NumPlayers();
  hvs_.reset(new unsigned int[num_players_]);
  winners_.reset(new bool[num_players_]);
  sum_pos_outcomes_.reset(new double[num_players_]);
  betting_tree_ = betting_tree;
  BoardTree::Create();
  BoardTree::CreateLookup();
  unsigned int max_street = Game::MaxStreet();
  a_probs_ = new CFRValues(nullptr, true, nullptr, betting_tree, 0, 0,
			   a_ca, a_buckets_->NumBuckets(), nullptr);
  b_probs_ = new CFRValues(nullptr, true, nullptr, betting_tree, 0, 0,
			   b_ca, b_buckets_->NumBuckets(), nullptr);

  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  a_ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  ba.BettingAbstractionName().c_str(),
	  a_cc.CFRConfigName().c_str());
  a_probs_->Read(dir, a_it, betting_tree->Root(), "x", kMaxUInt);
  fprintf(stderr, "Read A probs\n");
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  b_ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  ba.BettingAbstractionName().c_str(),
	  b_cc.CFRConfigName().c_str());
  b_probs_->Read(dir, b_it, betting_tree->Root(), "x", kMaxUInt);
  fprintf(stderr, "Read B probs\n");

  boards_ = new unsigned int[max_street + 1];
  boards_[0] = 0;
  raw_hcps_ = new unsigned int *[num_players_];
  for (unsigned int p = 0; p < num_players_; ++p) {
    raw_hcps_[p] = new unsigned int[max_street + 1];
  }

  if (a_buckets_->None(max_street) || b_buckets_->None(max_street)) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(max_street);
    unsigned int num_boards = BoardTree::NumBoards(max_street);
    sorted_hcps_ = new unsigned short *[num_boards];
    Card cards[7];
    unsigned int num_hole_cards = Game::NumCardsForStreet(0);
    unsigned int num_board_cards = Game::NumBoardCards(max_street);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
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

  for (unsigned int p = 0; p < num_players_; ++p) {
    sum_pos_outcomes_[p] = 0;
  }

  rand_bufs_ = new drand48_data[num_players_];
}

Player::~Player(void) {
  delete [] rand_bufs_;
  if (sorted_hcps_) {
    unsigned int max_street = Game::MaxStreet();
    unsigned int num_boards = BoardTree::NumBoards(max_street);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      delete [] sorted_hcps_[bd];
    }
    delete [] sorted_hcps_;
  }
  delete [] boards_;
  for (unsigned int p = 0; p < num_players_; ++p) {
    delete [] raw_hcps_[p];
  }
  delete [] raw_hcps_;
  if (b_buckets_ != a_buckets_) delete b_buckets_;
  delete a_buckets_;
  delete a_buckets_file_;
  delete b_buckets_file_;
  delete a_probs_;
  delete b_probs_;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <A card params> <B card params> "
	  "<betting abstraction params> <A CFR params> <B CFR params> "
	  "<A it> <B it> <num duplicate hands> "
	  "[determ|nondeterm] [mem|disk]\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 12) Usage(argv[0]);
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
  string marg = argv[11];
  bool mem_buckets;
  if (marg == "mem")       mem_buckets = true;
  else if (marg == "disk") mem_buckets = false;
  else                     Usage(argv[0]);

  HandValueTree::Create();
  fprintf(stderr, "Created HandValueTree\n");

  // Leave this in if we don't want reproducibility
  InitRand();
  BettingTree *betting_tree = BettingTree::BuildTree(*betting_abstraction);

  Player *player = new Player(betting_tree, *betting_abstraction,
			      *a_card_abstraction, *b_card_abstraction,
			      *a_cfr_config, *b_cfr_config, a_it, b_it,
			      mem_buckets);
  player->Go(num_duplicate_hands, deterministic);
  delete player;
  delete betting_tree;
}
