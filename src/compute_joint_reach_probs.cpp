// For each bucket in each betting state, compute the joint reach probability.
// Can be viewed as the probability that the next hand we play will reach
// that betting state.
//
// Stupidly reading all sumprobs twice.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

class Walker {
public:
  Walker(const CardAbstraction &card_abstraction,
	 const BettingAbstraction &betting_abstraction,
	 const CFRConfig &cfr_config, const Buckets &buckets, unsigned int it,
	 unsigned int final_st, unsigned int p);
  ~Walker(void);
  void Go(void);
private:
  void Walk(Node *node, unsigned int bd, double *p0_probs,
	    double *p1_probs, unsigned int last_st);
  void Write(Node *node, Writer **writers);
  void Write(void);
  
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  unsigned int it_;
  unsigned int final_st_;
  unique_ptr<BettingTree> betting_tree_;
  unique_ptr<CFRValues> sumprobs_;
  HandTree *hand_tree_;
  char dir_[500];
  unsigned int **bucket_counts_;
  double ***our_probs_;
  double ***opp_probs_;
  unsigned int p_;
};

Walker::Walker(const CardAbstraction &card_abstraction,
	       const BettingAbstraction &betting_abstraction,
	       const CFRConfig &cfr_config, const Buckets &buckets,
	       unsigned int it, unsigned int final_st, unsigned int p) :
  card_abstraction_(card_abstraction),
  betting_abstraction_(betting_abstraction), cfr_config_(cfr_config),
  buckets_(buckets) {
  it_ = it;
  unsigned int max_street = Game::MaxStreet();
  final_st_ = final_st;
  if (final_st_> max_street) final_st_ = max_street;
  p_ = p;
  
  if (betting_abstraction.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
#if 0
    betting_tree_.reset(
		 BettingTree::BuildAsymmetricTree(betting_abstraction, p));
#endif
  } else {
    betting_tree_.reset(BettingTree::BuildTree(betting_abstraction));
  }

  hand_tree_ = new HandTree(0, 0, final_st_);
  
  sprintf(dir_, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction.BettingAbstractionName().c_str(),
	  cfr_config.CFRConfigName().c_str());

#if 0
  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int pa = 0; pa < num_players; ++pa) {
    players[pa] = (p_ == pa);
  }
#endif
  unique_ptr<bool []> streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    streets[st] = st <= final_st_;
  }
  // Want sumprobs for both players.
  sumprobs_.reset(new CFRValues(nullptr, true, streets.get(),
				betting_tree_.get(), 0, 0, card_abstraction_,
				buckets_.NumBuckets(), nullptr));
  fprintf(stderr, "Reading sumprobs\n");
  sumprobs_->Read(dir_, it_, betting_tree_->Root(), "x", kMaxUInt);
  fprintf(stderr, "Read sumprobs\n");
  
  our_probs_ = new double **[final_st_ + 1];
  opp_probs_ = new double **[final_st_ + 1];
  for (unsigned int st = 0; st <= final_st_; ++st) {
    unsigned int num_nt = betting_tree_->NumNonterminals(p_, st);
    unsigned int num_buckets = buckets_.NumBuckets(st);
    our_probs_[st] = new double *[num_nt];
    opp_probs_[st] = new double *[num_nt];
    for (unsigned int i = 0; i < num_nt; ++i) {
      our_probs_[st][i] = new double[num_buckets];
      opp_probs_[st][i] = new double[num_buckets];
      for (unsigned int b = 0; b < num_buckets; ++b) {
	our_probs_[st][i][b] = 0;
	opp_probs_[st][i][b] = 0;
      }
    }
  }

  bucket_counts_ = new unsigned int *[final_st_ + 1];
  for (unsigned int st = 0; st <= final_st_; ++st) {
    unsigned int num_buckets = buckets_.NumBuckets(st);
    bucket_counts_[st] = new unsigned int[num_buckets];
    for (unsigned int b = 0; b < num_buckets; ++b) {
      bucket_counts_[st][b] = 0;
    }
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    for (unsigned int bd = 0; bd < num_boards; ++bd) {
      unsigned int board_count = BoardTree::BoardCount(st, bd);
      for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
	unsigned int h = bd * num_hole_card_pairs + i;
	unsigned int b = buckets_.Bucket(st, h);
	bucket_counts_[st][b] += board_count;
      }
    }
  }
}

Walker::~Walker(void) {
  for (unsigned int st = 0; st <= final_st_; ++st) {
    delete [] bucket_counts_[st];
  }
  delete [] bucket_counts_;
  
  for (unsigned int st = 0; st <= final_st_; ++st) {
    unsigned int num_nt = betting_tree_->NumNonterminals(p_, st);
    for (unsigned int i = 0; i < num_nt; ++i) {
      delete [] our_probs_[st][i];
      delete [] opp_probs_[st][i];
      }
    delete [] our_probs_[st];
    delete [] opp_probs_[st];
  }
  delete [] our_probs_;
  delete [] opp_probs_;

  delete hand_tree_;
}

static unsigned int Factorial(unsigned int n) {
  if (n == 0) return 1;
  if (n == 1) return 1;
  return n * Factorial(n - 1);
}

// What is the joint reach prob that I want to write out?  I don't think the
// number of hands in the bucket should be a factor.  But that *is* a factor
// in the raw joint probs calculated in Walk().
// Also want to divide by the number of opponent hole card pairs.
void Walker::Write(Node *node, Writer **writers) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > final_st_) return;
  unsigned int pa = node->PlayerActing();
  if (pa == p_) {
    unsigned int nt = node->NonterminalID();
    Writer *writer = writers[st];
    unsigned int num_board_cards = Game::NumBoardCards(st);
    unsigned int num_rem = Game::NumCardsInDeck() - num_board_cards -
      Game::NumCardsForStreet(0);
    unsigned int num_opp_hole_card_pairs = num_rem * (num_rem - 1) / 2;
    unsigned int num_buckets = buckets_.NumBuckets(st);
    for (unsigned int b = 0; b < num_buckets; ++b) {
      double norm_our = our_probs_[st][nt][b] / bucket_counts_[st][b];
      double norm_opp = opp_probs_[st][nt][b] / 
	(bucket_counts_[st][b] * num_opp_hole_card_pairs);
      // No values should be greater than 1.0, right?  Add sanity check.
      if (norm_our > 1.0) {
	fprintf(stderr, "Norm our: %f\n", norm_our);
	exit(-1);
      }
      if (norm_opp > 1.0) {
	fprintf(stderr, "Norm opp: %f raw %f bc %u nohcp %u st %u nt %u "
		"b %u\n", norm_opp, opp_probs_[st][nt][b],
		bucket_counts_[st][b], num_opp_hole_card_pairs, st, nt, b);
	exit(-1);
      }
      writer->WriteFloat(norm_our);
      writer->WriteFloat(norm_opp);
    }
  }
  unsigned int num_succs = node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Write(node->IthSucc(s), writers);
  }
}

void Walker::Write(void) {
  char buf[500];
  Writer **writers = new Writer *[final_st_ + 1];
  for (unsigned int st = 0; st <= final_st_; ++st) {
    sprintf(buf, "%s/joint_reach_probs.%u.%u.p%u", dir_, st, it_, p_);
    writers[st] = new Writer(buf);
  }
  Write(betting_tree_->Root(), writers);
  for (unsigned int st = 0; st <= final_st_; ++st) {
    delete writers[st];
  }
  delete [] writers;
}

void Walker::Walk(Node *node, unsigned int bd, double *p0_probs,
		  double *p1_probs, unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > final_st_) return;
  if (st > last_st) {
    unsigned int nbd_begin = BoardTree::SuccBoardBegin(last_st, bd, st);
    unsigned int nbd_end = BoardTree::SuccBoardEnd(last_st, bd, st);
    for (unsigned int nbd = nbd_begin; nbd < nbd_end; ++nbd) {
      if (st == 1) {
	fprintf(stderr, "Flop initial NT %u bd %u\n", node->NonterminalID(),
		nbd);
      }
      Walk(node, nbd, p0_probs, p1_probs, st);
    }
    return;
  }
  const CanonicalCards *hands = hand_tree_->Hands(st, bd);
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  if (pa == p_) {
    unsigned int opp = pa^1;
    unique_ptr<double []> opp_total_card_probs(new double[max_card1]);
    for (unsigned int i = 0; i < max_card1; ++i) {
      opp_total_card_probs[i] = 0;
    }
    double *our_probs = pa == 1 ? p1_probs : p0_probs;
    double *opp_probs = opp == 1 ? p1_probs : p0_probs;
    double sum_opp_probs = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double opp_prob = opp_probs[enc];
      if (opp_prob > 1.0) {
	fprintf(stderr, "opp_prob %f st %u bd %u enc %u\n", opp_prob, st,
		bd, enc);
	exit(-1);
      }
      opp_total_card_probs[hi] += opp_prob;
      opp_total_card_probs[lo] += opp_prob;
      sum_opp_probs += opp_prob;
    }
    unsigned int board_count = BoardTree::BoardCount(st, bd);
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = hands->Cards(i);
      Card hi = cards[0];
      Card lo = cards[1];
      unsigned int enc = hi * max_card1 + lo;
      double our_prob = our_probs[enc];
      double opp_prob = opp_probs[enc];
      // this_sum_opp is the sum of the reach probabilities of the opponent
      // hands that don't confict with <hi, lo>.
      double this_sum_opp = sum_opp_probs + opp_prob -
	opp_total_card_probs[hi] - opp_total_card_probs[lo];
      // double joint = our_prob * opp_reach;
      unsigned int h = bd * num_hole_card_pairs + i;
      unsigned int b = buckets_.Bucket(st, h);
      // joint_probs_[st][nt][b] += joint * board_count;
      our_probs_[st][nt][b] += our_prob * board_count;
      opp_probs_[st][nt][b] += this_sum_opp * board_count;
      if (opp_probs_[st][nt][b] > 1000000000.0) {
	fprintf(stderr, "tso %f bc %u\n", this_sum_opp, board_count);
	exit(-1);
      }
    }
  }
  unsigned int num_enc = max_card1 * max_card1;
  double **succ_probs = new double *[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    succ_probs[s] = new double[num_enc];
  }
  unsigned int dsi = node->DefaultSuccIndex();
  unique_ptr<double []> bucket_probs(new double[num_succs]);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    unsigned int h = bd * num_hole_card_pairs + i;
    unsigned int b = buckets_.Bucket(st, h);
    unsigned int offset = b * num_succs;
    sumprobs_->Probs(pa, st, nt, offset, num_succs, dsi, bucket_probs.get());
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (bucket_probs[s] > 1.0) {
	fprintf(stderr, "s %u bucket_prob %f\n", s, bucket_probs[s]);
	exit(-1);
      }
      if (pa == 0) {
	succ_probs[s][enc] = p0_probs[enc] * bucket_probs[s];
      } else {
	succ_probs[s][enc] = p1_probs[enc] * bucket_probs[s];
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (pa == 0) {
      Walk(node->IthSucc(s), bd, succ_probs[s], p1_probs, st);
    } else {
      Walk(node->IthSucc(s), bd, p0_probs, succ_probs[s], st);
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    delete [] succ_probs[s];
  }
  delete [] succ_probs;
}

void Walker::Go(void) {
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  unique_ptr<double []> p0_probs(new double[num_enc]);
  unique_ptr<double []> p1_probs(new double[num_enc]);
  for (unsigned int i = 0; i < num_enc; ++i) {
    p0_probs[i] = 1.0;
    p1_probs[i] = 1.0;
  }
  Walk(betting_tree_->Root(), 0, p0_probs.get(), p1_probs.get(), 0);
  Write();
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <final st>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 7) Usage(argv[0]);
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
  unique_ptr<CFRConfig>
    cfr_config(new CFRConfig(*cfr_params));
  unsigned int it, final_st;
  if (sscanf(argv[5], "%u", &it) != 1)       Usage(argv[0]);
  if (sscanf(argv[6], "%u", &final_st) != 1) Usage(argv[0]);

  BoardTree::Create();
  BoardTree::BuildBoardCounts();

  Buckets buckets(*card_abstraction, false);
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    Walker walker(*card_abstraction, *betting_abstraction, *cfr_config,
		  buckets, it, final_st, p);
    walker.Go();
  }
}
