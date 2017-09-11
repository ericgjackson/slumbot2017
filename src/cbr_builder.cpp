// How can I multithread this and write out the CBRs?  If I multithread,
// can't have each thread writing out the results as it calculates them.
// Would need to store in memory and then write out at end.  Possible for
// small games, but will we want to do this for large games where it
// doesn't fit in memory?
// For now run singlethreaded.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cbr_builder.h"
#include "cbr_thread.h"
#include "cfr_config.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"

using namespace std;

CBRBuilder::CBRBuilder(const CardAbstraction &ca, const BettingAbstraction &ba,
		       const CFRConfig &cc, const Buckets &buckets, bool cfrs,
		       unsigned int p, unsigned int it,
		       unsigned int num_threads) :
  card_abstraction_(ca), betting_abstraction_(ba), cfr_config_(cc) {
  num_threads_ = num_threads;
  BoardTree::Create();

  betting_tree_ = BettingTree::BuildTree(betting_abstraction_);
  
  HandValueTree::Create();
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_trunk_streets = CBRThread::kSplitStreet - 1;
  if (num_trunk_streets > max_street + 1) num_trunk_streets = max_street + 1;
  trunk_hand_tree_ = new HandTree(0, 0, num_trunk_streets - 1);
  
  if (Game::MaxStreet() < CBRThread::kSplitStreet) {
    threads_ = NULL;
  } else {
    threads_ = new CBRThread *[num_threads_];
    for (unsigned int i = 0; i < num_threads_; ++i) {
      threads_[i] = new CBRThread(card_abstraction_, betting_abstraction_,
				  cfr_config_, buckets, betting_tree_, cfrs, p,
				  trunk_hand_tree_, i, num_threads_, it, NULL,
				  false);
    }
  }
  
  trunk_thread_ = new CBRThread(card_abstraction_, betting_abstraction_,
				cfr_config_, buckets, betting_tree_, cfrs, p,
				trunk_hand_tree_, 0, num_threads_, it,
				threads_, true);

  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(), 
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    if (cfrs) {
      fprintf(stderr, "Don't support CFRs and asymmetric\n");
      exit(-1);
    }
    char buf2[100];
    sprintf(buf2, ".p%u", p);
    strcat(dir, buf2);
  }
  sprintf(buf, "%s/%s.%u.p%u", dir, cfrs ? "cfrs" : "cbrs", it, p);
  Mkdir(buf);
}

CBRBuilder::~CBRBuilder(void) {
  if (threads_) {
    for (unsigned int i = 0; i < num_threads_; ++i) {
      delete threads_[i];
    }
    delete [] threads_;
  }
  delete trunk_thread_;
  delete trunk_hand_tree_;
  delete betting_tree_;
}

void CBRBuilder::Go(void) {
  double ev = trunk_thread_->Go();
  printf("EV: %f\n", ev);
}

