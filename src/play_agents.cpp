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
#include "canonical.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
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
  Player(NLAgent *a_agent, NLAgent *b_agent, unsigned int small_blind);
  ~Player(void) {}
  void Go(unsigned long long int num_dup_hands, bool deterministic);
private:
  void Play(unsigned int b_pos, unsigned int *contributions,
	    unsigned int last_bet_to, bool *folded,
	    unsigned int num_remaining, unsigned int last_player_acting,
	    unsigned int st, bool street_initial, unsigned long long int h,
	    const Card *cards, const string &action_sequence,
	    double *outcomes);
  void PlayDuplicateHand(unsigned long long int h, const Card *cards,
			 bool deterministic,
			 double *a_sum, double *b_sum);
  void SetHCPsAndBoards(Card **raw_hole_cards, const Card *raw_board);
  
  NLAgent *a_agent_;
  NLAgent *b_agent_;
  unsigned int small_blind_;
  unsigned int num_players_;
  unsigned int *boards_;
  unsigned int **raw_hcps_;
  unique_ptr<unsigned int []> hvs_;
  unique_ptr<bool []> winners_;
};

Player::Player(NLAgent *a_agent, NLAgent *b_agent,
	       unsigned int small_blind) {
  a_agent_ = a_agent;
  b_agent_ = b_agent;
  small_blind_ = small_blind;
  num_players_ = Game::NumPlayers();
  hvs_.reset(new unsigned int[num_players_]);
  winners_.reset(new bool[num_players_]);
  unsigned int max_street = Game::MaxStreet();
  boards_ = new unsigned int[max_street + 1];
  boards_[0] = 0;
  raw_hcps_ = new unsigned int *[num_players_];
  for (unsigned int p = 0; p < num_players_; ++p) {
    raw_hcps_[p] = new unsigned int[max_street + 1];
  }
}

static unsigned int PrecedingPlayer(unsigned int p) {
  if (p == 0) return Game::NumPlayers() - 1;
  else        return p - 1;
}

void Player::Play(unsigned int b_pos, unsigned int *contributions,
		  unsigned int last_bet_to, bool *folded,
		  unsigned int num_remaining, unsigned int last_player_acting,
		  unsigned int st, bool street_initial,
		  unsigned long long int h, const Card *cards,
		  const string &action_sequence, double *outcomes) {
  if (num_remaining == 1) {
    // All players but one have folded
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
    return;
  }
  if (st == Game::MaxStreet() + 1) {
    // Showdown
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
    return;
  }

  // Find the next player to act.  Start with the first candidate and move
  // forward until we find someone who has not folded.  The first candidate
  // is either the last player plus one, or, if we are starting a new
  // betting round, the first player to act on that street.
  unsigned int actual_pa;
  if (street_initial) {
    actual_pa = Game::FirstToAct(st);
  } else {
    actual_pa = last_player_acting + 1;
  }
  while (true) {
    if (actual_pa == num_players_) actual_pa = 0;
    if (! folded[actual_pa]) break;
    ++actual_pa;
  }
  NLAgent *agent;
  if (actual_pa == b_pos) {
    agent = b_agent_;
  } else {
    agent = a_agent_;
  }

  char buf[100];
  string match_state = "MATCHSTATE";
  sprintf(buf, ":%u:%llu:%s:", actual_pa, h, action_sequence.c_str());
  match_state += buf;
  string cname;
  for (unsigned int p = 0; p < num_players_; ++p) {
    if (p == actual_pa) {
      CardName(cards[2*p], &cname);
      match_state += cname;
      CardName(cards[2*p+1], &cname);
      match_state += cname;
    }
    if (p < num_players_ - 1) {
      match_state += "|";
    }
  }
  if (st >= 1) {
    match_state += "/";
    unsigned int num_flop_cards = Game::NumBoardCards(1);
    for (unsigned int i = 0; i < num_flop_cards; ++i) {
      CardName(cards[2 * num_players_ + i], &cname);
      match_state += cname;
    }
    if (st >= 2) {
      match_state += "/";
      CardName(cards[2 * num_players_ + num_flop_cards], &cname);
      match_state += cname;
      if (st >= 3) {
	match_state += "/";
	CardName(cards[2 * num_players_ + num_flop_cards + 1], &cname);
	match_state += cname;
      }
    }
  }
  // fprintf(stderr, "%s\n", match_state.c_str());
  unsigned int bet_to;
  BotAction ba = agent->HandleStateChange(match_state, &bet_to);
  if (ba == BA_FOLD) {
    folded[actual_pa] = true;
    outcomes[actual_pa] = -(int)contributions[actual_pa];
    Play(b_pos, contributions, last_bet_to, folded, num_remaining - 1,
	 actual_pa, st, false, h, cards, action_sequence + "f", outcomes);
  } else if (ba == BA_CALL) {
    contributions[actual_pa] = last_bet_to;
    unsigned int new_st;
    bool new_street_initial;
    string new_action_sequence = action_sequence + "c";
    if (street_initial) {
      new_st = st;
      new_street_initial = false;
    } else {
      // This doesn't work for multiplayer
      new_st = st + 1;
      new_street_initial = true;
      new_action_sequence += "/";
    }
    Play(b_pos, contributions, last_bet_to, folded, num_remaining, actual_pa,
	 new_st, new_street_initial, h, cards, new_action_sequence,
	 outcomes);
  } else if (ba == BA_BET) {
    // unsigned int bet_size = bet_to - last_bet_to;
    sprintf(buf, "r%u", bet_to);
    string new_action_sequence = action_sequence + buf;
    contributions[actual_pa] = bet_to;
    Play(b_pos, contributions, bet_to, folded, num_remaining, actual_pa,
	 st, false, h, cards, new_action_sequence, outcomes);
  } else {
    fprintf(stderr, "Unexpected ba %u\n", ba);
    exit(-1);
  }
}

// Play one hand of duplicate, which is a pair of regular hands.  Return
// outcome from A's perspective.
void Player::PlayDuplicateHand(unsigned long long int h, const Card *cards,
			       bool deterministic,
			       double *a_sum, double *b_sum) {
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
    for (unsigned int p = 0; p < num_players_; ++p) {
      folded[p] = false;
      if (p == small_blind_p) {
	contributions[p] = Game::SmallBlind() * small_blind_;
      } else if (p == big_blind_p) {
	contributions[p] = Game::BigBlind() * small_blind_;
      } else {
	contributions[p] = 0;
      }
    }
#if 0
    // I will now do this in NLAgent
    if (deterministic) {
      // Seed so we can get the same cards and compare results.
      SeedRand(h);
    }
#endif
    // Use the same hand index (h) so we will get the same RNG draws for
    // each call to Play().  But call SetNewHand() on each agent so that we
    // know a new hand is beginning.  (Normally we look to the new hand
    // index to know that a new hand has begun.)
    a_agent_->SetNewHand();
    b_agent_->SetNewHand();
    Play(b_pos, contributions.get(), Game::BigBlind() * small_blind_,
	 folded.get(), num_players_, 1000, 0, true, h, cards, "",
	 outcomes.get());
    // fprintf(stderr, "Outcomes:");
    for (unsigned int p = 0; p < num_players_; ++p) {
      // fprintf(stderr, " %f", outcomes[p]);
      if (p == b_pos) {
	*b_sum += outcomes[p];
      } else {
	*a_sum += outcomes[p];
      }
      // sum_pos_outcomes_[p] += outcomes[p];
    }
    // fprintf(stderr, "\n");
  }
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

void Player::Go(unsigned long long int num_dup_hands, bool deterministic) {
  Card cards[100], hand_cards[7];
  Card **hole_cards = new Card *[num_players_];
  for (unsigned int p = 0; p < num_players_; ++p) {
    hole_cards[p] = new Card[2];
  }
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_board_cards = Game::NumBoardCards(max_street);
  double sum_a_outcomes = 0, sum_b_outcomes = 0;
  double sum_sqd_a_outcomes = 0, sum_sqd_b_outcomes = 0;

  for (unsigned long long int h = 0; h < num_dup_hands; ++h) {
    if (deterministic) {
      // Seed so we can get the same cards and compare results.
      SeedRand(h);
    }
    // Assume 2 hole cards
    DealNCards(cards, num_board_cards + 2 * num_players_);
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
    
    double a_outcome, b_outcome;
    PlayDuplicateHand(h, cards, deterministic, &a_outcome, &b_outcome);
    sum_a_outcomes += a_outcome;
    sum_b_outcomes += b_outcome;
    sum_sqd_a_outcomes += a_outcome * a_outcome;
    sum_sqd_b_outcomes += b_outcome * b_outcome;
  }

  // Divide by num_players because we evaluate B that many times (once for
  // each position).
  unsigned long long int num_b_hands = num_dup_hands * num_players_;
  double mean_b_outcome = sum_b_outcomes / (double)num_b_hands;
  // Need to divide by 100 (big blind) to convert from chips to units of the
  // big blinds.
  // Multiply by 1000 to go from big blinds to milli-big-blinds
  double big_blind = 2 * small_blind_;
  double b_mbb_g = (mean_b_outcome / big_blind) * 1000.0;
  printf("Avg B outcome: %f (%.1f mbb/g) over %llu dup hands\n",
	 mean_b_outcome, b_mbb_g, num_dup_hands);
  // Variance is the mean of the squares minus the square of the means
  double var_b =
    (sum_sqd_b_outcomes / ((double)num_b_hands)) -
    (mean_b_outcome * mean_b_outcome);
  double stddev_b = sqrt(var_b);
  double match_stddev = stddev_b * sqrt(num_b_hands);
  double match_lower = sum_b_outcomes - 1.96 * match_stddev;
  double match_upper = sum_b_outcomes + 1.96 * match_stddev;
  double mbb_lower =
    ((match_lower / (num_b_hands)) / big_blind) * 1000.0;
  double mbb_upper =
    ((match_upper / (num_b_hands)) / big_blind) * 1000.0;
  printf("MBB confidence interval: %f-%f\n", mbb_lower, mbb_upper);
  fflush(stdout);

#if 0
  for (unsigned int p = 0; p < num_players_; ++p) {
    double avg_outcome =
      sum_pos_outcomes_[p] / (double)(num_players_ * num_dup_hands);
    printf("Avg P%u outcome: %f\n", p, avg_outcome);
    fflush(stdout);
  }
#endif
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
	  "<B num endgame its> <B its> <num dup hands> [determ|nondeterm] "
	  "(optional args)\n", prog_name);
  fprintf(stderr, "Optional arguments:\n");
  fprintf(stderr, "  debug: generate debugging output\n");
  fprintf(stderr, "  eoe: exit on error\n");
  fprintf(stderr, "  fs: fixed seed\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc < 24) {
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

  unsigned long long int num_dup_hands;
  if (sscanf(argv[a++], "%llu", &num_dup_hands) != 1) Usage(argv[0]);
  bool determ;
  string darg = argv[a++];
  if (darg == "determ")         determ = true;
  else if (darg == "nondeterm") determ = false;
  else                          Usage(argv[0]);
  
  bool debug = false;             // Disabled by default
  bool exit_on_error = false;     // Disabled by default
  for (int i = a; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "debug") {
      debug = true;
    } else if (arg == "eoe") {
      exit_on_error = true;
    } else {
      Usage(argv[0]);
    }
  }

  InitRand();

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

  BoardTree::Create();
  BoardTree::CreateLookup();
  HandValueTree::Create();

  // No fixed seed; will handle seeding here.
  unsigned int small_blind = 50;
  unsigned int stack_size = 20000;
  bool fixed_seed = true;
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

  Player player(&a_agent, &b_agent, small_blind);
  player.Go(num_dup_hands, determ);

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
