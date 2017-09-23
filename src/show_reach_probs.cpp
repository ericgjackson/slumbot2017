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
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

class Processor {
public:
  Processor(const CardAbstraction &ca, const BettingAbstraction &ba,
	    const CFRConfig &cc, unsigned int it, unsigned int target_st,
	    unsigned int target_pa, unsigned int target_nt,
	    unsigned int target_bd);
  ~Processor(void) {}
  void Go(void);
private:
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  unique_ptr<Buckets> buckets_;
  unsigned int it_;
  unsigned int target_st_;
  unsigned int target_pa_;
  unsigned int target_nt_;
  unsigned int target_bd_;
  unique_ptr<BettingTree> betting_tree_;
  unique_ptr<HandTree> hand_tree_;
  unique_ptr<CFRValues> sumprobs_;

  double ***GetSuccReachProbs(Node *node, unsigned int bd,
			      double **reach_probs);
  void Walk(Node *node, const string &action_sequence, unsigned int bd,
	    double **reach_probs, unsigned int last_st);
};

Processor::Processor(const CardAbstraction &ca, const BettingAbstraction &ba,
		     const CFRConfig &cc, unsigned int it,
		     unsigned int target_st, unsigned int target_pa,
		     unsigned int target_nt, unsigned int target_bd) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc) {
  it_ = it;
  target_st_ = target_st;
  target_pa_ = target_pa;
  target_nt_ = target_nt;
  target_bd_ = target_bd;

  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  } else {
    fprintf(stderr, "ccc1\n");
    betting_tree_.reset(BettingTree::BuildTree(betting_abstraction_));
    fprintf(stderr, "ccc2\n");
  }

  unsigned int max_street = Game::MaxStreet();
  fprintf(stderr, "ccc3\n");
  hand_tree_.reset(new HandTree(0, 0, max_street));
  fprintf(stderr, "ccc4\n");
}

// bd is a global board index
double ***Processor::GetSuccReachProbs(Node *node, unsigned int bd,
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
  const CanonicalCards *hands = hand_tree_->Hands(st, bd);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    unsigned int offset;
    if (buckets_->None(st)) {
      offset = bd * num_hole_card_pairs * num_succs + i * num_succs;
    } else {
      unsigned int h = bd * num_hole_card_pairs + i;
      unsigned int b = buckets_->Bucket(st, h);
      offset = b * num_succs;
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      double prob = sumprobs_->Prob(pa, st, nt, offset, s, num_succs, dsi);
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

// Actually, could I just iterate over all boards at each node.
void Processor::Walk(Node *node, const string &action_sequence,
		     unsigned int bd, double **reach_probs,
		     unsigned int last_st) {
  fprintf(stderr, "www1\n");
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    unsigned int nbd_begin = BoardTree::SuccBoardBegin(st-1, bd, st);
    unsigned int nbd_end = BoardTree::SuccBoardEnd(st-1, bd, st);
    for (unsigned int nbd = nbd_begin; nbd < nbd_end; ++nbd) {
      Walk(node, action_sequence, nbd, reach_probs, st);
    }
    return;
  }
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  if (st == target_st_ && pa == target_pa_ && nt == target_nt_ &&
      bd == target_bd_) {
    const CanonicalCards *hands = hand_tree_->Hands(st, bd);
    unsigned int num_players = Game::NumPlayers();
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int maxcard1 = Game::MaxCard() + 1; 
    for (unsigned int p = 0; p < num_players; ++p) {
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	const Card *our_cards = hands->Cards(i);
	Card our_hi = our_cards[0];
	Card our_lo = our_cards[1];
	unsigned int our_enc = our_hi * maxcard1 + our_lo;
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
	printf("P%u ", p);
	OutputTwoCards(our_hi, our_lo);
	printf(" our reach %f sop %f\n", reach_probs[p][our_enc],
	       sum_opp_probs);
      }
    }
  }

  unsigned int num_succs = node->NumSuccs();
  double ***succ_reach_probs = GetSuccReachProbs(node, bd, reach_probs);
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Walk(node->IthSucc(s), action_sequence + action, bd, succ_reach_probs[s],
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
  const CanonicalCards *preflop_hands = hand_tree_->Hands(0, 0);
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
  buckets_.reset(new Buckets(card_abstraction_, false));
  sumprobs_.reset(new CFRValues(nullptr, true, nullptr, betting_tree_.get(),
				0, 0, card_abstraction_, *buckets_,
				nullptr));
  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  }
  sumprobs_->Read(dir, it_, betting_tree_->Root(),
		  betting_tree_->Root()->NonterminalID(), kMaxUInt);
  
  Walk(betting_tree_->Root(), "", 0, reach_probs, 0);

  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <st> <pa> <nt> <bd>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 10) Usage(argv[0]);
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
  unsigned int it, st, pa, nt, bd;
  if (sscanf(argv[5], "%u", &it) != 1) Usage(argv[0]);
  if (sscanf(argv[6], "%u", &st) != 1) Usage(argv[0]);
  if (sscanf(argv[7], "%u", &pa) != 1) Usage(argv[0]);
  if (sscanf(argv[8], "%u", &nt) != 1) Usage(argv[0]);
  if (sscanf(argv[9], "%u", &bd) != 1) Usage(argv[0]);

  Processor processor(*card_abstraction, *betting_abstraction, *cfr_config,
		      it, st, pa, nt, bd);
  processor.Go();
}
