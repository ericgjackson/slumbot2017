#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>

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
#include "io.h"
#include "params.h"

using namespace std;

class Walker {
public:
  Walker(const CardAbstraction &card_abstraction,
	 const BettingAbstraction &betting_abstraction,
	 const CFRConfig &cfr_config, const Buckets &buckets, unsigned int it,
	 unsigned int final_st);
  ~Walker(void);
  void Go(void);
private:
  void Walk(Node *node);
  
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  unsigned int it_;
  unsigned int final_st_;
  unique_ptr<BettingTree> betting_tree_;
  Reader ***readers1_;
  Reader ***readers2_;
  unsigned long long int num_our_match_;
  unsigned long long int num_our_mismatch_;
  unsigned long long int num_opp_match_;
  unsigned long long int num_opp_mismatch_;
};

Walker::Walker(const CardAbstraction &card_abstraction,
	       const BettingAbstraction &betting_abstraction,
	       const CFRConfig &cfr_config, const Buckets &buckets,
	       unsigned int it, unsigned int final_st) :
  card_abstraction_(card_abstraction),
  betting_abstraction_(betting_abstraction), cfr_config_(cfr_config),
  buckets_(buckets) {
  it_ = it;
  unsigned int max_street = Game::MaxStreet();
  final_st_ = final_st;
  if (final_st_> max_street) final_st_ = max_street;
  
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

  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction.BettingAbstractionName().c_str(),
	  cfr_config.CFRConfigName().c_str());

  unsigned int num_players = Game::NumPlayers();
  readers1_ = new Reader **[final_st_ + 1];
  readers2_ = new Reader **[final_st_ + 1];
  for (unsigned int st = 0; st <= final_st_; ++st) {
    readers1_[st] = new Reader *[num_players];
    readers2_[st] = new Reader *[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      sprintf(buf, "%s/joint_reach_probs.%u.%u.p%u", dir, st, it_, p);
      readers1_[st][p] = new Reader(buf);
      sprintf(buf, "%s/joint_reach_probs2.%u.%u.p%u", dir, st, it_, p);
      readers2_[st][p] = new Reader(buf);
    }
  }

  num_our_match_ = 0;
  num_our_mismatch_ = 0;
  num_opp_match_ = 0;
  num_opp_mismatch_ = 0;
}

Walker::~Walker(void) {
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int st = 0; st <= final_st_; ++st) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete readers1_[st][p];
      delete readers2_[st][p];
    }
    delete [] readers1_[st];
    delete [] readers2_[st];
  }
  delete [] readers1_;
  delete [] readers2_;
}

void Walker::Walk(Node *node) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int st = node->Street();
  if (st > final_st_) return;
  unsigned int pa = node->PlayerActing();
  Reader *reader1 = readers1_[st][pa];
  Reader *reader2 = readers2_[st][pa];
  unsigned int num_buckets = buckets_.NumBuckets(st);
  for (unsigned int b = 0; b < num_buckets; ++b) {
    float our_reach1 = reader1->ReadFloatOrDie();
    float opp_reach1 = reader1->ReadFloatOrDie();
    float our_reach2 = reader2->ReadFloatOrDie();
    float opp_reach2 = reader2->ReadFloatOrDie();
    if (our_reach1 < 999 && our_reach2 < 999) {
      double rel_our = our_reach1 / our_reach2;
      if (rel_our > 1.0) rel_our = 1.0 / rel_our;
      double abs_our = std::abs(our_reach1 - our_reach2);
      if (rel_our < 0.9 && abs_our > 0.2) {
	printf("Our mismatch\n");
	printf("our1 %f our2 %f\n", our_reach1, our_reach2);
	printf("st %u pa %u nt %u b %u\n", st, pa, node->NonterminalID(), b);
	fflush(stdout);
	++num_our_mismatch_;
      } else {
	++num_our_match_;
      }
    }
    if (opp_reach1 < 999 && opp_reach2 < 999) {
      double rel_opp = opp_reach1 / opp_reach2;
      if (rel_opp > 1.0) rel_opp = 1.0 / rel_opp;
      double abs_opp = std::abs(opp_reach1 - opp_reach2);
      if (rel_opp < 0.9 && abs_opp > 0.1) {
	printf("Opp mismatch\n");
	printf("opp1 %f opp2 %f\n", opp_reach1, opp_reach2);
	printf("st %u pa %u nt %u b %u\n", st, pa, node->NonterminalID(), b);
	fflush(stdout);
	++num_opp_mismatch_;
      } else {
	++num_opp_match_;
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    Walk(node->IthSucc(s));
  }
}

void Walker::Go(void) {
  Walk(betting_tree_->Root());
  fprintf(stderr, "%.2f%% our match (%llu/%llu)\n",
	  100.0 * ((double)num_our_match_) /
	  (double)(num_our_match_ + num_our_mismatch_),
	  num_our_match_, num_our_match_ + num_our_mismatch_);
  fprintf(stderr, "%.2f%% opp match (%llu/%llu)\n",
	  100.0 * ((double)num_opp_match_) /
	  (double)(num_opp_match_ + num_opp_mismatch_),
	  num_opp_match_, num_opp_match_ + num_opp_mismatch_);

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

  Buckets buckets(*card_abstraction, true);
  Walker walker(*card_abstraction, *betting_abstraction, *cfr_config,
		buckets, it, final_st);
  walker.Go();
}
