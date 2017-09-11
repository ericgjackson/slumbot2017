// This is a work in progress.
//
// Do I want to do compression?  Maybe add it later?
//
// Should I create these every time they are needed?  That might be
// easiest.  What about the hand_tree_?  Expensive to create it on every
// iteration.  But it requires memory.  Maybe best to create it once, in which
// case we should only create these subgame objects once.
//
// Actually, as things stand now, we will have lots of copies of the same
// hand tree.  Maybe better to recreate subgame objects on each iteration.
// This will simplify some other things as well.
//
// Should I save subgame files from checkpoint iterations?  I used to.
//
// Can I have a subgame for every *turn* board, not every *flop* board?  I
// don't see why not.

#include <math.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cards.h"
#include "cfr_config.h"
#include "cfr_utils.h"
#include "cfr_values.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"
#include "vcfr.h"
#include "vcfr_subgame.h"

using namespace std;

VCFRSubgame::VCFRSubgame(const CardAbstraction &ca,
			 const BettingAbstraction &ba, const CFRConfig &cc,
			 const Buckets &buckets, Node *root,
			 unsigned int root_bd, unsigned int subtree_nt,
			 VCFR *cfr) :
  VCFR(ca, ba, cc, buckets, nullptr, 1) {
  subgame_ = true;
  root_ = root;
  root_bd_ = root_bd;
  root_bd_st_ = root->Street() - 1;
  subtree_nt_ = subtree_nt;
  cfr_ = cfr;
  
  unsigned int max_street = Game::MaxStreet();

  subtree_ = BettingTree::BuildSubtree(root);
  
  unsigned int subtree_st = root->Street();
  subtree_streets_ = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    subtree_streets_[st] = st >= subtree_st;
  }

  hand_tree_ = new HandTree(root_bd_st_, root_bd_, max_street);
  opp_probs_ = nullptr;
}

VCFRSubgame::~VCFRSubgame(void) {
  // Do not delete final_vals_; it has been passed to the parent VCFR object
  delete [] opp_probs_;
  delete [] subtree_streets_;
  delete hand_tree_;
  delete subtree_;
}

void VCFRSubgame::SetOppProbs(double *opp_probs) {
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  opp_probs_ = new double[num_enc];
  for (unsigned int i = 0; i < num_enc; ++i) {
    opp_probs_[i] = opp_probs[i];
  }
}

// Don't delete files from last_checkpoint_it
void VCFRSubgame::DeleteOldFiles(unsigned int it) {
  if (it_ < 3) {
    // Don't delete anything on first two iterations
    return;
  }
  unsigned int delete_it = it_ - 2;
  if (delete_it == last_checkpoint_it_) return;
  
  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    sprintf(buf, ".p%u", target_p_);
    strcat(dir, buf);
  }
  unsigned int max_street = Game::MaxStreet();
  unsigned int subtree_st = subtree_->Root()->Street();
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (! subtree_streets_[st]) continue;
    // Remove the regret file created for the current player two iterations ago
    sprintf(buf, "%s/regrets.%u.%u.%u.%u.%u.%u.p%u.i", dir,
	    subtree_st, subtree_nt_, root_bd_st_, root_bd_, st, delete_it, p_);
#if 0
    if (! FileExists(buf)) {
      // It should exist.  Test for debugging purposes.
      fprintf(stderr, "DeleteOldFiles: %s does not exist\n", buf);
      exit(-1);
    }
#endif
    RemoveFile(buf);

#if 0
    // I don't think I need this any more.
    
    // In the P1 phase of iteration 3, we want to remove P2 sumprobs from
    // iteration 1.
    // In the P2 phase of iteration 2, we want to remove P1 sumprobs from
    // iteration 1.
    // The P1 phase precedes the P2 phase in running CFR.
    unsigned int last_it = 0;
    if (p_ && it >= 3) {
      last_it = it - 2;
    } else if (! p_ && it >= 2) {
      last_it = it - 1;
    }
#endif
    sprintf(buf, "%s/sumprobs.%u.%u.%u.%u.%u.%u.p%u.i", dir,
	    subtree_st, subtree_nt_, root_bd_st_, root_bd_, st, delete_it,
	    p_^1);
#if 0
    if (! FileExists(buf)) {
      // It should exist.  Test for debugging purposes.
      fprintf(stderr, "DeleteOldFiles: %s does not exist\n", buf);
      exit(-1);
    }
#endif
    RemoveFile(buf);
  }
}

void VCFRSubgame::Go(void) {
  if (! value_calculation_) {
    DeleteOldFiles(it_);
  }
  unsigned int subtree_st = subtree_->Root()->Street();

  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  card_abstraction_.CardAbstractionName().c_str(), Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction_.BettingAbstractionName().c_str(),
	  cfr_config_.CFRConfigName().c_str());
  if (betting_abstraction_.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", target_p_);
    strcat(dir, buf);
  }

  unsigned int num_players = Game::NumPlayers();
  unique_ptr<bool []> sp_players(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    sp_players[p] = p != p_;
  }
  if (value_calculation_) {
    // Only need the opponent's sumprobs
    sumprobs_.reset(new CFRValues(sp_players.get(), true, subtree_streets_,
				  subtree_, root_bd_, root_bd_st_,
				  card_abstraction_, buckets_,
				  compressed_streets_));
    sumprobs_->Read(dir, it_, subtree_->Root(), subtree_nt_, kMaxUInt);
  } else {
    // Need both players regrets
    regrets_.reset(new CFRValues(nullptr, false, subtree_streets_,
				 subtree_, root_bd_, root_bd_st_,
				 card_abstraction_, buckets_,
				 compressed_streets_));
    // Only need the opponent's sumprobs
    sumprobs_.reset(new CFRValues(sp_players.get(), true, subtree_streets_,
				  subtree_, root_bd_, root_bd_st_,
				  card_abstraction_, buckets_,
				  compressed_streets_));

    if (it_ == 1) {
      if (p_) {
	// It 1 P1 phase: initialize P0 and P1 regrets
	regrets_->AllocateAndClearInts(subtree_->Root(), kMaxUInt);
      } else {
	// It 1 P0 phase: read P1 regrets from disk; initialize P0 regrets
	regrets_->Read(dir, it_, subtree_->Root(), subtree_nt_, 1);
	regrets_->AllocateAndClearInts(subtree_->Root(), 0);
      }
    } else {
      if (p_) {
	// Read regrets for both players from previous iteration
	regrets_->Read(dir, it_ - 1, subtree_->Root(), subtree_nt_, kMaxUInt);
      } else {
	// Read P1 regrets from current iteration
	// Read P0 regrets from previous iteration
	regrets_->Read(dir, it_, subtree_->Root(), subtree_nt_, 1);
	regrets_->Read(dir, it_ - 1, subtree_->Root(), subtree_nt_, 0);
      }
    }
    if (it_ == 1) {
      sumprobs_->AllocateAndClearInts(subtree_->Root(), kMaxUInt);
    } else {
      sumprobs_->Read(dir, it_ - 1, subtree_->Root(), subtree_nt_, kMaxUInt);
    }
  }

  unsigned int max_card = Game::MaxCard();
  double *total_card_probs = new double[max_card + 1];
  double sum_opp_probs;
  const CanonicalCards *hands = hand_tree_->Hands(root_bd_st_, 0);
  CommonBetResponseCalcs(root_bd_st_, hands, opp_probs_, &sum_opp_probs,
			 total_card_probs);
  final_vals_ = Process(subtree_->Root(), 0, opp_probs_, sum_opp_probs,
			total_card_probs, subtree_st - 1);

  delete [] total_card_probs;

  if (! value_calculation_) {
    Mkdir(dir);
    regrets_->Write(dir, it_, subtree_->Root(), subtree_nt_, p_);
    sumprobs_->Write(dir, it_, subtree_->Root(), subtree_nt_, kMaxUInt);
  }

  // This should delete the regrets and sumprobs, no?
  regrets_.reset(nullptr);
  sumprobs_.reset(nullptr);

  cfr_->Post(thread_index_);
}
