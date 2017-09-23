// mean_cbrs should be indexed by board.  Output should include board.

#include <math.h>
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
  Processor(const CardAbstraction &ca0, const CardAbstraction &ca1,
	    const BettingAbstraction &ba, const CFRConfig &cc0,
	    const CFRConfig &cc1, unsigned int it0, unsigned int it1);
  ~Processor(void);
  void Go(void);
private:
  const CardAbstraction &card_abstraction0_;
  const CardAbstraction &card_abstraction1_;
  const CardAbstraction *card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config0_;
  const CFRConfig &cfr_config1_;
  const CFRConfig *cfr_config_;
  unique_ptr<Buckets> buckets_;
  unsigned int it0_;
  unsigned int it1_;
  unsigned int it_;
  unique_ptr<BettingTree> betting_tree_;
  unique_ptr<HandTree> hand_tree_;
  unique_ptr<CFRValues> sumprobs_;
  // Indexed by system, player, street, player acting, NT ID and board.
  double ******mean_cbrs_;

  double *LoadCBRs(Node *node, unsigned int bd, unsigned int p);
  double ***GetSuccReachProbs(Node *node, unsigned int bd,
			      double **reach_probs);
  void Walk(Node *node, const string &action_sequence, unsigned int bd,
	    unsigned int sys, double **reach_probs, unsigned int last_st);
  void Compare(Node *node, const string &action_sequence, unsigned int bd,
	       unsigned int last_st);
};

Processor::Processor(const CardAbstraction &ca0, const CardAbstraction &ca1,
		     const BettingAbstraction &ba,
		     const CFRConfig &cc0, const CFRConfig &cc1,
		     unsigned int it0, unsigned int it1) :
  card_abstraction0_(ca0), card_abstraction1_(ca1),
  betting_abstraction_(ba), cfr_config0_(cc0), cfr_config1_(cc1) {
  it0_ = it0;
  it1_ = it1;

  if (betting_abstraction_.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  } else {
    betting_tree_.reset(BettingTree::BuildTree(betting_abstraction_));
  }

  unsigned int max_street = Game::MaxStreet();
  hand_tree_.reset(new HandTree(0, 0, max_street));

  // Indicate by system, player, street, player acting and NT ID.
  unsigned int num_players = Game::NumPlayers();
  mean_cbrs_ = new double *****[2];
  for (unsigned int y = 0; y < 2; ++y) {
    mean_cbrs_[y] = new double ****[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      mean_cbrs_[y][p] = new double ***[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	unsigned int num_boards = BoardTree::NumBoards(st);
	mean_cbrs_[y][p][st] = new double **[num_players];
	for (unsigned int pa = 0; pa < num_players; ++pa) {
	  unsigned int num_nt = betting_tree_->NumNonterminals(pa, st);
	  mean_cbrs_[y][p][st][pa] = new double *[num_nt];
	  for (unsigned int i = 0; i < num_nt; ++i) {
	    mean_cbrs_[y][p][st][pa][i] = new double[num_boards];
	    for (unsigned int bd = 0; bd < num_boards; ++bd) {
	      mean_cbrs_[y][p][st][pa][i][bd] = 999999999.9;
	    }
	  }
	}
      }
    }
  }
}

Processor::~Processor(void) {
  unsigned int num_players = Game::NumPlayers();
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int y = 0; y < 2; ++y) {
    for (unsigned int p = 0; p < num_players; ++p) {
      for (unsigned int st = 0; st <= max_street; ++st) {
	for (unsigned int pa = 0; pa < num_players; ++pa) {
	  unsigned int num_nt = betting_tree_->NumNonterminals(pa, st);
	  for (unsigned int i = 0; i < num_nt; ++i) {
	    delete [] mean_cbrs_[y][p][st][pa][i];
	  }
	  delete [] mean_cbrs_[y][p][st][pa];
	}
	delete [] mean_cbrs_[y][p][st];
      }
      delete [] mean_cbrs_[y][p];
    }
    delete [] mean_cbrs_[y];
  }
  delete [] mean_cbrs_;
}

// Note we want to load the opponent's CVs.  So if target_p_ is 1, we
// load P2's CVs.
double *Processor::LoadCBRs(Node *node, unsigned int bd, unsigned int p) {
#if 0
  bool bucketed = false;
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    const string &bk = card_abstraction_->Bucketing(st);
    if (bk != "none") {
      bucketed = true;
      break;
    }
  }
  if (! card_level && ! bucketed) {
    fprintf(stderr, "Can't use bucket-level CVs if base had no card "
	    "abstraction\n");
    exit(-1);
  }
#endif

  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int pa = node->PlayerActing();
  char dir[500], buf[500];
  // This assumes two players
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s/cbrs.%u.p%u/%u.%u.%u",
	  Files::OldCFRBase(), Game::GameName().c_str(),
	  card_abstraction_->CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_->CFRConfigName().c_str(), it_, p, nt, st, pa);
  sprintf(buf, "%s/vals.%u", dir, bd);
	  
  Reader reader(buf);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  double *cvs = new double[num_hole_card_pairs];
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    cvs[i] = reader.ReadFloatOrDie();
  }

  return cvs;
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
		     unsigned int bd, unsigned int sys, double **reach_probs,
		     unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    unsigned int nbd_begin = BoardTree::SuccBoardBegin(st-1, bd, st);
    unsigned int nbd_end = BoardTree::SuccBoardEnd(st-1, bd, st);
    for (unsigned int nbd = nbd_begin; nbd < nbd_end; ++nbd) {
      Walk(node, action_sequence, nbd, sys, reach_probs, st);
    }
    return;
  }
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int maxcard1 = Game::MaxCard() + 1; 
  const CanonicalCards *hands = hand_tree_->Hands(st, bd);
  for (unsigned int p = 0; p < num_players; ++p) {
    double *cbrs = LoadCBRs(node, bd, p);
    double sum_joint_probs = 0;
    double sum_weighted_cbrs = 0;
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
    if (sum_joint_probs > 0) {
      double mean_cbr = sum_weighted_cbrs / sum_joint_probs;
      mean_cbrs_[sys][p][st][pa][nt][bd] = mean_cbr;
    }
    delete [] cbrs;
  }
  unsigned int num_succs = node->NumSuccs();
  double ***succ_reach_probs = GetSuccReachProbs(node, bd, reach_probs);
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Walk(node->IthSucc(s), action_sequence + action, bd, sys,
	 succ_reach_probs[s], st);
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

// Need to handle street transition
void Processor::Compare(Node *node, const string &action_sequence,
			unsigned int bd, unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    unsigned int nbd_begin = BoardTree::SuccBoardBegin(st-1, bd, st);
    unsigned int nbd_end = BoardTree::SuccBoardEnd(st-1, bd, st);
    for (unsigned int nbd = nbd_begin; nbd < nbd_end; ++nbd) {
      Compare(node, action_sequence, nbd, st);
    }
    return;
  }
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    double mean0 = mean_cbrs_[0][p][st][pa][nt][bd];
    double mean1 = mean_cbrs_[1][p][st][pa][nt][bd];
    if (mean0 < 999999999 && mean1 < 999999999) {
      double diff = mean0 - mean1;
      printf("%f %s P%u st %u pa %u nt %u bd %u (%f, %f)\n", diff,
	     action_sequence.c_str(), p, st, pa, nt, bd, mean0, mean1);
    }
  }
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    Compare(node->IthSucc(s), action_sequence + action, bd, last_st);
  }
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
  for (unsigned int sys = 0; sys < 2; ++sys) {
    card_abstraction_ = sys == 0 ? &card_abstraction0_ : &card_abstraction1_;
    cfr_config_ = sys == 0 ? &cfr_config0_ : &cfr_config1_;
    it_ = sys == 0 ? it0_ : it1_;
    buckets_.reset(new Buckets(*card_abstraction_, false));
    sumprobs_.reset(new CFRValues(nullptr, true, nullptr, betting_tree_.get(),
				  0, 0, *card_abstraction_, *buckets_,
				  nullptr));
    char dir[500];
    sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(),
	    card_abstraction_->CardAbstractionName().c_str(),
	    Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	    betting_abstraction_.BettingAbstractionName().c_str(),
	    cfr_config_->CFRConfigName().c_str());
    if (betting_abstraction_.Asymmetric()) {
      fprintf(stderr, "Asymmetric not supported yet\n");
      exit(-1);
    }
    sumprobs_->Read(dir, it_, betting_tree_->Root(),
		    betting_tree_->Root()->NonterminalID(), kMaxUInt);

    Walk(betting_tree_->Root(), "", 0, sys, reach_probs, 0);
  }
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;

  Compare(betting_tree_->Root(), "", 0, 0);
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params0> <card params1> "
	  "<betting params> <CFR params0> <CFR params1> <it0> <it1> "
	  "<num threads>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 10) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_params0 = CreateCardAbstractionParams();
  card_params0->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    card_abstraction0(new CardAbstraction(*card_params0));
  unique_ptr<Params> card_params1 = CreateCardAbstractionParams();
  card_params1->ReadFromFile(argv[3]);
  unique_ptr<CardAbstraction>
    card_abstraction1(new CardAbstraction(*card_params1));
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unique_ptr<Params> cfr_params0 = CreateCFRParams();
  cfr_params0->ReadFromFile(argv[5]);
  unique_ptr<CFRConfig> cfr_config0(new CFRConfig(*cfr_params0));
  unique_ptr<Params> cfr_params1 = CreateCFRParams();
  cfr_params1->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig> cfr_config1(new CFRConfig(*cfr_params1));
  unsigned int it0, it1, num_threads;
  if (sscanf(argv[7], "%u", &it0) != 1)         Usage(argv[0]);
  if (sscanf(argv[8], "%u", &it1) != 1)         Usage(argv[0]);
  if (sscanf(argv[9], "%u", &num_threads) != 1) Usage(argv[0]);

  BoardTree::Create();
  Processor processor(*card_abstraction0, *card_abstraction1,
		      *betting_abstraction, *cfr_config0, *cfr_config1, it0,
		      it1);
  processor.Go();
}
