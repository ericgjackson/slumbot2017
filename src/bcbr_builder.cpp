// How can I multithread this?  May need locks on terminal_bucket_vals_ and
// si_bucket_vals_.  Writing out will also be a problem if we try to
// multithread the second pass.
//
// For now run singlethreaded.

#include <stdio.h>
#include <stdlib.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "bcbr_builder.h"
#include "bcbr_thread.h"
#include "bcfr_thread.h"
#include "cfr_config.h"
#include "cfr_values.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"

using namespace std;

BCBRBuilder::BCBRBuilder(const CardAbstraction &ca,
			 const BettingAbstraction &ba, const CFRConfig &cc,
			 const Buckets &buckets, bool cfrs, unsigned int p,
			 unsigned int it, unsigned int num_threads) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc) {
  num_threads_ = num_threads;
  BoardTree::Create();

  betting_tree_ = BettingTree::BuildTree(betting_abstraction_);
  HandValueTree::Create();
  unsigned int max_street = Game::MaxStreet();
  // For now
  unsigned int num_trunk_streets = max_street + 1;
  trunk_hand_tree_ = new HandTree(0, 0, num_trunk_streets - 1);
  
  threads_ = nullptr;
#if 0
  threads_ = new BCBRThread *[num_threads_];
  for (unsigned int i = 0; i < num_threads_; ++i) {
    threads_[i] = new BCBRThread(card_abstraction_, betting_abstraction_,
				 cfr_config_, buckets, betting_tree_, p,
				 trunk_hand_tree_, i, num_threads_, it, NULL,
				 false);
  }
#endif

  trunk_cbr_thread_ = nullptr;
  trunk_cfr_thread_ = nullptr;
  
  if (cfrs) {
    trunk_cfr_thread_ = new BCFRThread(card_abstraction_, betting_abstraction_,
				       cfr_config_, buckets, betting_tree_, p,
				       trunk_hand_tree_, 0, num_threads_, it,
				       nullptr, true);
  } else {
    trunk_cbr_thread_ = new BCBRThread(card_abstraction_, betting_abstraction_,
				       cfr_config_, buckets, betting_tree_, p,
				       trunk_hand_tree_, 0, num_threads_, it,
				       threads_, true);
  }

  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str());
  sprintf(buf, "%s/%s.%u.p%u", dir, cfrs ? "bcfrs" : "bcbrs", it, p);
  Mkdir(buf);
}

BCBRBuilder::~BCBRBuilder(void) {
  if (threads_) {
    for (unsigned int i = 0; i < num_threads_; ++i) {
      delete threads_[i];
    }
    delete [] threads_;
  }
  delete trunk_cbr_thread_;
  delete trunk_cfr_thread_;
  delete trunk_hand_tree_;
  delete betting_tree_;
}

void BCBRBuilder::Go(void) {
  if (trunk_cbr_thread_) {
    trunk_cbr_thread_->Go();
  } else if (trunk_cfr_thread_) {
    trunk_cfr_thread_->Go();
  }
}

