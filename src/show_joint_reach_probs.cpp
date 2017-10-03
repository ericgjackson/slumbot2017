// This seems to be much too slow for full Holdem with a reasonable betting
// abstraction like mb3b3.  Even truncating at turn-initial, it looks like
// it's going to take a while.
// Sampling 1/100 flop boards and truncating at turn initial - this may be
// enough to make time reasonable.  64m.

#include <stdio.h>
#include <stdlib.h>

#include <unordered_map>

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

struct Item {
  double sum_joint_probs;
  unsigned int sum_board_counts;
  unsigned int st;
};

class Processor {
public:
  Processor(const CardAbstraction &ca, const BettingAbstraction &ba,
	    const CFRConfig &cc, unsigned int it, unsigned int incr);
  ~Processor(void) {}
  void Go(void);
  const HandTree &GetHandTree(unsigned int st) {
    if (st == 0) return *preflop_hand_tree_.get();
    else         return *postflop_hand_tree_.get();
  }
private:
  double ***GetSuccReachProbs(Node *node, unsigned int gbd,
			      double **reach_probs);
  void Walk(Node *node, unsigned int gbd, double **reach_probs,
	    const string &action_sequence, unsigned int last_st);
  
  double sample_prob_;
  bool always_call_;
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  Buckets buckets_;
  unsigned int it_;
  unsigned int incr_;
  unique_ptr<BettingTree> betting_tree_;
  unique_ptr<HandTree> preflop_hand_tree_;
  unsigned int flop_bd_;
  unique_ptr<HandTree> postflop_hand_tree_;
  unique_ptr<CFRValues> sumprobs_;
  unordered_map<string, Item> map_;
};

Processor::Processor(const CardAbstraction &ca, const BettingAbstraction &ba,
		     const CFRConfig &cc, unsigned int it, unsigned int incr) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc),
  buckets_(ca, false) {
  it_ = it;
  incr_ = incr;

  unsigned int max_street = Game::MaxStreet();
  
  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  } else {
    betting_tree_.reset(BettingTree::BuildTree(betting_abstraction_));
  }
  
  // We need probs for both players
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
  sumprobs_.reset(new CFRValues(nullptr, true, nullptr,
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
  sumprobs_->Read(dir, it_, betting_tree_->Root(), "x", kMaxUInt);
  preflop_hand_tree_.reset(new HandTree(0, 0, 0));
}

double ***Processor::GetSuccReachProbs(Node *node, unsigned int gbd,
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
  if (nt >= sumprobs_->NumNonterminals(pa, st)) {
    fprintf(stderr, "GetSuccReachProbs: OOB nt %u numnt %u\n", nt,
	    sumprobs_->NumNonterminals(pa, st));
    exit(-1);
  }
  unsigned int dsi = node->DefaultSuccIndex();
  const HandTree &hand_tree = GetHandTree(st);
  unsigned int lbd = st == 0 ? 0 : BoardTree::LocalIndex(1, flop_bd_, st, gbd);
  const CanonicalCards *hands = hand_tree.Hands(st, lbd);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    unsigned int offset;
    if (buckets_.None(st)) {
      offset = gbd * num_hole_card_pairs * num_succs + i * num_succs;
    } else {
      unsigned int h = gbd * num_hole_card_pairs + i;
      unsigned int b = buckets_.Bucket(st, h);
      offset = b * num_succs;
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      double prob =
	sumprobs_->Prob(pa, st, nt, offset, s, num_succs, dsi);
      if (prob > 1.0) {
	fprintf(stderr, "Prob > 1\n");
	fprintf(stderr, "num_succs %u gbd %u nhcp %u hcp %u\n", num_succs,
		gbd, num_hole_card_pairs, i);
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

void Processor::Walk(Node *node, unsigned int gbd, double **reach_probs,
		     const string &action_sequence, unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(last_st, gbd, st);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(last_st, gbd, st);
    if (st == 1) {
      for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ngbd += incr_) {
	flop_bd_ = ngbd;
	postflop_hand_tree_.reset(new HandTree(1, flop_bd_, Game::MaxStreet()));
	fprintf(stderr, "Walk %s bd %u\n", action_sequence.c_str(), ngbd);
	Walk(node, ngbd, reach_probs, action_sequence, st);
      }
    } else {
      for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
	Walk(node, ngbd, reach_probs, action_sequence, st);
      }
    }
    return;
  }
  double sum_joint_probs = 0;
  const HandTree &hand_tree = GetHandTree(st);
  unsigned int lbd =
    st == 0 ? 0 : BoardTree::LocalIndex(1, flop_bd_, st, gbd);
  const CanonicalCards *hands = hand_tree.Hands(st, lbd);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int maxcard1 = Game::MaxCard() + 1;
  // Normalize probs so that sum_joint_probs is 1 if everyone reaches.
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *p0_cards = hands->Cards(i);
    Card p0_hi = p0_cards[0];
    Card p0_lo = p0_cards[1];
    unsigned int p0_enc = p0_hi * maxcard1 + p0_lo;
    double p0_prob = reach_probs[0][p0_enc] / num_hole_card_pairs;
    double sum_p1_probs = 0;
    unsigned int num_opp_hole_card_pairs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *p1_cards = hands->Cards(j);
      Card p1_hi = p1_cards[0];
      Card p1_lo = p1_cards[1];
      if (p1_hi == p0_hi || p1_hi == p0_lo || p1_lo == p0_hi ||
	  p1_lo == p0_lo) {
	continue;
      }
      ++num_opp_hole_card_pairs;
      unsigned int p1_enc = p1_hi * maxcard1 + p1_lo;
      double p1_prob = reach_probs[1][p1_enc];
      sum_p1_probs += p1_prob;
    }
    sum_joint_probs += p0_prob * (sum_p1_probs / num_opp_hole_card_pairs);
  }
  unsigned int board_count = BoardTree::BoardCount(st, gbd);
  unordered_map<string, Item>::iterator it;
  it = map_.find(action_sequence);
  if (it == map_.end()) {
    Item item;
    item.sum_joint_probs = sum_joint_probs * board_count;
    item.sum_board_counts = board_count;
    item.st = st;
    map_[action_sequence] = item;
  } else {
    it->second.sum_joint_probs += sum_joint_probs * board_count;
    it->second.sum_board_counts += board_count;
  }
  if (st == 2) {
    // Return at turn-initial nodes
    return;
  }
  
  unsigned int num_succs = node->NumSuccs();
  double ***succ_reach_probs = GetSuccReachProbs(node, gbd, reach_probs);
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Walk(node->IthSucc(s), gbd, succ_reach_probs[s], action_sequence + action,
	 st);
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

void Processor::Go(void) {
  unsigned int num_players = Game::NumPlayers();
  double **reach_probs = new double *[num_players];
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  const CanonicalCards *preflop_hands = preflop_hand_tree_->Hands(0, 0);
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
  Walk(betting_tree_->Root(), 0, reach_probs, "x", 0);
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;

  unordered_map<string, Item>::iterator it;
  for (it = map_.begin(); it != map_.end(); ++it) {
    Item item = it->second;
    double avg = item.sum_joint_probs / item.sum_board_counts;
    printf("%f %s %u\n", avg, it->first.c_str(), item.st);
  }
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <num threads> <it> [current|avg] <incr>\n", prog_name);
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
  unsigned int incr;
  if (sscanf(argv[8], "%u", &incr) != 1) Usage(argv[0]);
  
  BoardTree::Create();
  BoardTree::BuildBoardCounts();
  Processor processor(*card_abstraction, *betting_abstraction, *cfr_config,
		      it, incr);
  processor.Go();
}
