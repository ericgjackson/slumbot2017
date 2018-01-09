#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "files.h"
#include "game.h"
#include "io.h"
#include "joint_reach_probs.h"

JointReachProbs::JointReachProbs(const CardAbstraction &ca,
				 const BettingAbstraction &ba,
				 const CFRConfig &cc,
				 const unsigned int *num_buckets,
				 unsigned int it,
				 unsigned int final_st) {
  final_st_ = final_st;

  betting_tree_.reset(BettingTree::BuildTree(ba));

  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  ba.BettingAbstractionName().c_str(),
	  cc.CFRConfigName().c_str());

  unsigned int num_players = Game::NumPlayers();
  our_reach_probs_ = new float ***[num_players];
  opp_reach_probs_ = new float ***[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    our_reach_probs_[p] = new float **[final_st_ + 1];
    opp_reach_probs_[p] = new float **[final_st_ + 1];
    for (unsigned int st = 0; st <= final_st_; ++st) {
      unsigned int st_num_buckets = num_buckets[st];
      unsigned int num_nt = betting_tree_->NumNonterminals(p, st);
      our_reach_probs_[p][st] = new float *[num_nt];
      opp_reach_probs_[p][st] = new float *[num_nt];
      sprintf(buf, "%s/joint_reach_probs.%u.%u.p%u", dir, st, it, p);
      Reader reader(buf);
      for (unsigned int i = 0; i < num_nt; ++i) {
	our_reach_probs_[p][st][i] = new float[st_num_buckets];
	opp_reach_probs_[p][st][i] = new float[st_num_buckets];
	for (unsigned int b = 0; b < st_num_buckets; ++b) {
	  our_reach_probs_[p][st][i][b] = reader.ReadFloatOrDie();
	  opp_reach_probs_[p][st][i][b] = reader.ReadFloatOrDie();
	}
      }
    }
  }
}

JointReachProbs::~JointReachProbs(void) {
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    for (unsigned int st = 0; st <= final_st_; ++st) {
      unsigned int num_nt = betting_tree_->NumNonterminals(p, st);
      for (unsigned int i = 0; i < num_nt; ++i) {
	delete [] our_reach_probs_[p][st][i];
	delete [] opp_reach_probs_[p][st][i];
      }
      delete [] our_reach_probs_[p][st];
      delete [] opp_reach_probs_[p][st];
    }
    delete [] our_reach_probs_[p];
    delete [] opp_reach_probs_[p];
  }
  delete [] our_reach_probs_;
  delete [] opp_reach_probs_;
}

