// The NLAgent class implements the logic for playing poker based on
// probabilities computed by some algorithm like CFR.  It handles mapping
// the cards to buckets, looking up the probabilities, selecting an action
// according to the probabilities, flooring and translation.
//
// The NLAgent class doesn't know about networking or the ACPC protocol.  That
// logic is elsewhere.
//
// Don't need buckets on the river, I don't think.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "acpc_protocol.h"
#include "betting_abstraction.h"
#include "betting_tree.h"
#include "betting_tree_builder.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_values_file.h"
#include "constants.h"
#include "dynamic_cbr2.h"
#include "eg_cfr.h"
#include "endgame_utils.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "hand_value_tree.h"
#include "io.h"
#include "nl_agent.h"
#include "rand.h"
#include "resolving_method.h"
#include "runtime_config.h"

using namespace std;

// Assume no bet pending
BettingTree *NLAgent::CreateSubtree(Node *node, unsigned int target_p,
				    bool base) {
  unsigned int player_acting = node->PlayerActing();
  unsigned int bet_to = node->LastBetTo();
  unsigned int st = node->Street();
  unsigned int last_bet_size = 0;
  unsigned int num_street_bets = 0;
  unsigned int num_terminals = 0;
  // Only need initial street, stack size and min bet from
  // base_betting_abstraction_.
  const BettingAbstraction &betting_abstraction = base ?
    base_betting_abstraction_ : endgame_betting_abstraction_;
  BettingTreeBuilder betting_tree_builder(betting_abstraction, target_p);
  shared_ptr<Node> subtree_root =
    betting_tree_builder.CreateNoLimitSubtree(st, last_bet_size, bet_to,
					      num_street_bets, player_acting,
					      target_p, &num_terminals);
  return BettingTree::BuildSubtree(subtree_root.get());
}

// Need to set reach_probs
void NLAgent::ResolveSubgame(unsigned int p, unsigned int bd,
			     double **reach_probs) {
  unsigned int num_players = Game::NumPlayers();
  if (debug_) {
    unsigned int max_card1 = Game::MaxCard() + 1;
    // Temporary
    for (unsigned int p = 0; p < num_players; ++p) {
      double sum = 0;
      const Card *board = BoardTree::Board(endgame_st_, bd);
      unsigned int num_board_cards = Game::NumBoardCards(endgame_st_);
      for (Card hi = 1; hi < max_card1; ++hi) {
	if (InCards(hi, board, num_board_cards)) continue; 
	for (Card lo = 0; lo < hi; ++lo) {
	  if (InCards(lo, board, num_board_cards)) continue;
	  unsigned int enc = hi * max_card1 + lo;
	  sum += reach_probs[p][enc];
	}
      }
      fprintf(stderr, "P%u sum-reach-probs %f\n", p, sum);
    }
  }

  unsigned int max_street = Game::MaxStreet();
  unsigned int num_path = path_->size();
  HandTree hand_tree(endgame_st_, bd, Game::MaxStreet());
  if (num_path == 0) {
    fprintf(stderr, "ResolveSubgame: empty path?!?\n");
    exit(-1);
  }
  Node *si_node = (*path_)[num_path - 1];
  if (si_node->Street() != endgame_st_) {
    fprintf(stderr,
	    "ResolveSubgame: last node on path not on endgame street?!?\n");
    exit(-1);
  }

  BettingTree *base_subtree = CreateSubtree(si_node, p, true);
  if (debug_) {
    fprintf(stderr, "Created subtree\n");
  }
  unique_ptr<double []> t_vals;
  bool t_cfrs = false, t_zero_sum = true, current = true;
  // This is a little confusing, but we actually want to set pure to false.
  // Setting pure to true, in combination with current, will cause the FTL
  // method to be applied to the regrets.  But, actually, in ReadPureSubtree(),
  // we have created regret values that are 1 for the best-succ and 0 for
  // the other succs.  So we want to use the prob method PURE or
  // REGRET_MATCHING.
  bool pure = false;
  unique_ptr<bool []> base_streets(new bool[max_street + 1]);
  for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
    base_streets[st1] = (st1 >= endgame_st_);
  }
  // We need both players because we are computing zero-sum T values
  CFRValues base_regrets(nullptr, false, base_streets.get(),
			 base_subtree, bd, endgame_st_, base_card_abstraction_,
			 buckets_->NumBuckets(), nullptr);
  if (debug_) {
    fprintf(stderr, "Created base regrets\n");
  }
  char dir[500], buf[500];
  sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(),
	  base_cfr_config_.CFRConfigName().c_str());
  if (base_betting_abstraction_.Asymmetric()) {
    sprintf(buf, ".p%u", p);
    strcat(dir, buf);
  }
  if (debug_) fprintf(stderr, "Calling ReadPureSubtree\n");
  probs_[p]->ReadPureSubtree(si_node, base_subtree, &base_regrets);
  if (debug_) fprintf(stderr, "Back from ReadPureSubtree\n");

  t_vals.reset(dynamic_cbr_->Compute(base_subtree->Root(), reach_probs, bd,
				     &hand_tree, endgame_st_, bd, p^1, t_cfrs,
				     t_zero_sum, current, pure, &base_regrets,
				     nullptr));
  delete base_subtree;
  delete endgame_subtree_;
  endgame_subtree_ = CreateSubtree(si_node, p, false);
  // Switch the street initial node for the endgame street to the root of
  // the endgame subtree.
  (*path_)[num_path-1] = endgame_subtree_->Root();
  delete endgame_sumprobs_;

  unique_ptr<bool []> subtree_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    subtree_streets[st] = st >= endgame_st_;
  }
  unique_ptr<bool []> players(new bool[num_players]);
  for (unsigned int p1 = 0; p1 < num_players; ++p1) {
    players[p1] = p1 == p;
  }
  endgame_sumprobs_ = new CFRValues(players.get(), true, subtree_streets.get(),
				    endgame_subtree_, bd, endgame_st_,
				    endgame_card_abstraction_,
				    endgame_buckets_->NumBuckets(),
				    nullptr);

  endgame_sumprobs_->AllocateAndClearDoubles(endgame_subtree_->Root(),
					     kMaxUInt);
  ResolvingMethod method = ResolvingMethod::COMBINED;
  bool cfrs = false, zero_sum = true;
  EGCFR eg_cfr(endgame_card_abstraction_, endgame_betting_abstraction_,
	       endgame_cfr_config_, *endgame_buckets_, endgame_st_, method,
	       cfrs, zero_sum, 1);
  eg_cfr.SolveSubgame(endgame_subtree_, bd, reach_probs, "x", &hand_tree,
		      t_vals.get(), p, false, num_endgame_its_,
		      endgame_sumprobs_);
}

// Currently assume that this is a street-initial node.
// Might need to do up to four solves.  Imagine we have an asymmetric base
// betting tree, and an asymmetric solving method.
void NLAgent::ResolveAndWrite(Node *node, unsigned int gbd,
			      const string &action_sequence,
			      double **reach_probs) {
  unsigned int st = node->Street();
  fprintf(stderr, "Resolve %s st %u nt %u gbd %u\n",
	  action_sequence.c_str(), st, node->NonterminalID(), gbd);

  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    path_->resize(1);
    (*path_)[0] = node;
    ResolveSubgame(p, gbd, reach_probs);

    // Assume symmetric system for now
    ResolvingMethod method = ResolvingMethod::COMBINED;
    WriteEndgame(endgame_subtree_->Root(), action_sequence, action_sequence,
		 gbd, base_card_abstraction_, endgame_card_abstraction_,
		 base_betting_abstraction_, endgame_betting_abstraction_,
		 base_cfr_config_, endgame_cfr_config_, method,
		 endgame_sumprobs_, st, gbd, p, p, st);
  }
}

// In order to do translation, find the two succs that most closely match
// the current action.
void NLAgent::GetTwoClosestSuccs(Node *node, unsigned int actual_bet_to,
				 unsigned int *below_succ,
				 unsigned int *below_bet_to,
				 unsigned int *above_succ,
				 unsigned int *above_bet_to) {
  unsigned int num_succs = node->NumSuccs();
  // Want to find closest bet below and closest bet above
  unsigned int csi = node->CallSuccIndex();
  unsigned int fsi = node->FoldSuccIndex();
  if (debug_) {
    fprintf(stderr, "s %i pa %u nt %i ns %i fsi %i csi %i\n", node->Street(),
	    node->PlayerActing(), node->NonterminalID(), node->NumSuccs(),
	    fsi, csi);
  }
  *below_succ = kMaxUInt;
  *below_bet_to = kMaxUInt;
  *above_succ = kMaxUInt;
  *above_bet_to = kMaxUInt;
  unsigned int best_below_diff = kMaxUInt;
  unsigned int best_above_diff = kMaxUInt;
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (s == fsi) continue;
    unsigned int this_bet_to = node->IthSucc(s)->LastBetTo() * small_blind_;
    int diff = (int)this_bet_to - ((int)actual_bet_to);
    if (debug_) {
      fprintf(stderr, "s %i this_bet_to %u actual_bet_to %u diff %i\n", s,
	      this_bet_to, actual_bet_to, diff);
    }
    if (diff <= 0) {
      if (((unsigned int)-diff) < best_below_diff) {
	best_below_diff = -diff;
	*below_succ = s;
	*below_bet_to = this_bet_to;
      }
    } else {
      if (((unsigned int)diff) < best_above_diff) {
	best_above_diff = diff;
	*above_succ = s;
	*above_bet_to = this_bet_to;
      }
    }
  }
  if (debug_) {
    fprintf(stderr, "Best below %i diff %i\n", *below_succ, best_below_diff);
    fprintf(stderr, "Best above %i diff %i\n", *above_succ, best_above_diff);
  }
}

double NLAgent::BelowProb(unsigned int actual_bet_to,
			  unsigned int below_bet_to,
			  unsigned int above_bet_to,
			  unsigned int actual_pot_size) {
  double below_prob;
  if (translation_method_ == 0 || translation_method_ == 1) {
    // Express bet sizes as fraction of pot
    unsigned int last_bet_to = actual_pot_size / 2;
    int actual_bet = ((int)actual_bet_to) - ((int)last_bet_to);
    double d_actual_pot_size = actual_pot_size;
    double actual_frac = actual_bet / d_actual_pot_size;
    // below_bet could be negative, I think, so I make all these bet_to
    // quantities signed integers.
    int below_bet = ((int)below_bet_to) - ((int)last_bet_to);
    double below_frac = below_bet / d_actual_pot_size;
    if (below_frac < -1.0) {
      fprintf(stderr, "below_frac %f\n", below_frac);
      if (exit_on_error_) exit(-1);
      return 0;
    }
    int above_bet = ((int)above_bet_to) - ((int)last_bet_to);
    double above_frac = above_bet / d_actual_pot_size;
    below_prob =
      ((above_frac - actual_frac) *
       (1.0 + below_frac)) /
      ((above_frac - below_frac) *
       (1.0 + actual_frac));
    if (debug_) fprintf(stderr, "Raw below prob: %f\n", below_prob);
    if (translation_method_ == 1) {
      // Translate to nearest
      if (below_prob < 0.5) below_prob = 0;
      else                  below_prob = 1.0;
    }
  } else {
    fprintf(stderr, "Unknown translation method %i\n", translation_method_);
    if (exit_on_error_) exit(-1);
    else                below_prob = 0.5;
  }
  return below_prob;
}

#if 0
// If opp has made a bet that we are treating as an all-in (but which is
// actually less than all-in) then instead of just calling, we should reraise
// all-in.
bool NLAgent::ForceAllIn(Node *last_node, unsigned int actual_opp_bet_to) {
  // Only applies after an opponent bet
  if (actual_opp_bet_to == 0) return false;

  // Did we map opponent's bet to an all-in?
  if (last_node->LastBetTo() * small_blind_ != stack_size_) {
    return false;
  }
    // Is his actual bet less than an all-in?
  if (actual_opp_bet_to == stack_size_) {
    return false;
  }

  if (last_node->Street() < 3) {
    if (debug_) fprintf(stderr, "Forcing all in\n");
    return true;
  } else {
    // We'll skip all that logic from below
    if (debug_) fprintf(stderr, "Not forcing all in\n");
    return false;
  }
}
#endif

// Perform translation (if necessary) on the opponent's new action.
// Generate our response (if any).
// Can be called multiple times if there are multiple new opponent actions to
// process.  There can't be multiple new bets though.
void NLAgent::Translate(Action a, Node **sob_node,
			unsigned int actual_pot_size) {
  *sob_node = NULL;
  Node *node = (*path_)[path_->size() - 1];
  if (debug_) {
    fprintf(stderr, "Translate action type %i\n", a.action_type);
  }

  if (a.action_type == CALL) {
    if (debug_) fprintf(stderr, "Translating call\n");
    unsigned int s = node->CallSuccIndex();
    if (s == kMaxUInt) {
      if (node->Terminal()) {
	// This can happen if there was a previous bet that got rounded
	// up to all-in.
	return;
      } else {
	fprintf(stderr, "No call succ?!?\n");
	exit(-1);
      }
    }
    Node *node2 = node->IthSucc(s);
    path_->push_back(node2);
    if (debug_) {
      fprintf(stderr, "Adding node st %u pa %u nt %u\n",
	      node2->Street(), node2->PlayerActing(), node2->NonterminalID());
    }
  } else if (a.action_type == FOLD) {
    if (debug_) fprintf(stderr, "Translating fold\n");
    unsigned int s = node->FoldSuccIndex();
    if (s == kMaxUInt) {
      // This can happen if the player previously made a large bet that we
      // translated up to an all-in.  He's not really all-in and folds to a
      // following raise.
      s = node->CallSuccIndex();
      if (s == kMaxUInt) {
	if (node->Terminal()) {
	  // This can happen if there was a previous bet that got rounded
	  // up to all-in.
	  return;
	} else {
	  fprintf(stderr, "No call succ?!?\n");
	  exit(-1);
	}
      }
    }
    Node *node2 = node->IthSucc(s);
    path_->push_back(node2);
    if (debug_) {
      fprintf(stderr, "Adding node st %u pa %u nt %u\n",
	      node2->Street(), node2->PlayerActing(), node2->NonterminalID());
    }
  } else {
    // We are facing a bet by the opponent.  Need to perform translation.
    // The pot size before the opponent's bet
    unsigned int actual_opp_bet_to = a.bet_to;
    unsigned int below_bet_to, above_bet_to;
    unsigned int below_succ, above_succ;
    // Find the two closest "bets" in our abstraction.  One will have a
    // bet size <= the actual bet size; one will have a bet size >= the
    // actual bet size.  We treat a check/call as a bet of size zero.
    // In some cases, there are not two possible succs in which case either
    // below_succ or above_succ will be kMaxUInt.  In some cases there will not
    // be any possible bets in which case the below succ will be the call
    // succ and the above succ will be kMaxUInt.
    GetTwoClosestSuccs(node, actual_opp_bet_to, &below_succ, &below_bet_to,
		       &above_succ, &above_bet_to);
    unsigned int call_succ = node->CallSuccIndex();
    // Special case if we are facing a bet that is smaller than the smallest
    // bet in our abstraction.  The below succ here is actually a check
    // or a call in the abstraction.
    //
    // The code here appears to allow for the possibility that the above
    // succ may be the call succ.  How could that be?  I think that can
    // occur when actual_bet_to is less than the current within-abstraction
    // pot size.  This can occur when a prior small bet is mapped up to a
    // larger bet in the abstraction.
    //
    // We shouldn't blend two actions in this situation.  We should use the
    // single closest succ.  If translate_bet_to_call is true, we should use
    // the call succ.
    if (below_succ == call_succ || above_succ == call_succ) {
      if (debug_) {
	fprintf(stderr, "Of two closest succs, one is check/call\n");
      }
      unsigned int smallest_bet_succ = kMaxUInt;
      if (below_succ == call_succ) {
	if (above_succ != kMaxUInt) {
	  smallest_bet_succ = above_succ;
	}
      } else {
	// Some corner cases here.
	unsigned int num_succs = node->NumSuccs();
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == call_succ) continue;
	  if (s == node->FoldSuccIndex()) continue;
	  smallest_bet_succ = s;
	  break;
	}
#if 0
	// This can happen in six-player
	if (smallest_bet_succ == kMaxUInt) {
	  fprintf(stderr, "No smallest bet succ?!?\n");
	  fprintf(stderr, "below_succ %u above_succ %u call_succ %u\n",
		  below_succ, above_succ, call_succ);
	  fprintf(stderr, "st %i pa %u nt %i\n", node->Street(),
		  node->PlayerActing(), node->NonterminalID());
	  if (exit_on_error_) exit(-1);
	  else                return;
	}
#endif
      }
      Node *smallest_bet_node = NULL;
      if (smallest_bet_succ != kMaxUInt) {
	smallest_bet_node = node->IthSucc(smallest_bet_succ);
      }
#if 0
      // When can smallest_bet_succ be unset?
      if (smallest_bet_succ == kMaxUInt) {
	fprintf(stderr, "smallest_bet_succ unset?!?\n");
	fprintf(stderr, "below_succ %i above_succ %i call_succ %i\n",
		below_succ, above_succ, call_succ);
	fprintf(stderr, "st %u p%u nt %i\n", node->Street(),
		node->PlayerActing(), node->NonterminalID());
	fprintf(stderr, "action type %i bet-to %i\n", a.action_type,
		a.bet_to);
	if (exit_on_error_) exit(-1);
	else                return;
      }
#endif
      if (translate_bet_to_call_ && ! translate_to_larger_) {
	unsigned int selected_succ;
	// Sometimes we have only one valid succ.
	if (above_succ == kMaxUInt) {
	  selected_succ = below_succ;
	} else if (below_succ == kMaxUInt) {
	  selected_succ = above_succ;
	} else {
	  // Opponent's bet size is between two bets in our abstraction.
	  double below_prob = BelowProb(actual_opp_bet_to, below_bet_to,
					above_bet_to, actual_pot_size);
	  double r;
	  // r = RandZeroToOne();
	  drand48_r(&rand_bufs_[node->PlayerActing()], &r);
	  if (debug_) {
	    fprintf(stderr, "pa %u path sz %i r %f\n",
		    node->PlayerActing(), (int)path_->size(), r);
	  }
	  if (r < below_prob) {
	    if (debug_) {
	      fprintf(stderr, "Selected below bet; succ %i\n", below_succ);
	    }
	    selected_succ = below_succ;
	  } else {
	    if (debug_) {
	      fprintf(stderr, "Selected above bet; succ %i\n", above_succ);
	    }
	    selected_succ = above_succ;
	  }
	}
	if (debug_) fprintf(stderr, "selected_succ %i\n", selected_succ);
	if (selected_succ == call_succ) {
	  Node *call_node = node->IthSucc(call_succ);
	  path_->push_back(call_node);
	  if (debug_) {
	    fprintf(stderr, "Adding node st %u pa %u nt %u\n",
		    call_node->Street(),
		    call_node->PlayerActing(), call_node->NonterminalID());
	  }
	  // Don't use above_succ; it might be kMaxUInt
	  *sob_node = smallest_bet_node;
	  if (debug_) fprintf(stderr, "sob_node is succ %u\n",
			      smallest_bet_succ);
	} else {
	  Node *bet_node = node->IthSucc(selected_succ);
	  if (debug_) {
	    fprintf(stderr, "Adding node st %u pa %u nt %u\n",
		    bet_node->Street(),
		    bet_node->PlayerActing(), bet_node->NonterminalID());
	  }
	  path_->push_back(bet_node);
	}
      } else {
	if (smallest_bet_node) {
	  if (debug_) fprintf(stderr, "Mapping to smallest bet succ %u\n",
			      smallest_bet_succ);
	  // Map this bet to the smallest bet in our abstraction
	  if (debug_) {
	    fprintf(stderr, "Adding node st %u pa %u nt %u\n",
		    smallest_bet_node->Street(),
		    smallest_bet_node->PlayerActing(),
		    smallest_bet_node->NonterminalID());
	  }
	  path_->push_back(smallest_bet_node);
	} else {
	  Node *call_node = node->IthSucc(call_succ);
	  if (debug_) {
	    fprintf(stderr, "Adding node st %u pa %u nt %u\n",
		    call_node->Street(),
		    call_node->PlayerActing(), call_node->NonterminalID());
	  }
	  path_->push_back(call_node);
	}
      }
    } else {
      unsigned int selected_succ;
      if (above_succ == kMaxUInt) {
	// Can happen if we do not have all-ins in our betting abstraction
	selected_succ = below_succ;
	if (debug_) {
	  fprintf(stderr, "Selected below bet; succ %i\n", below_succ);
	}
      } else if (below_succ == kMaxUInt) {
	// There should always be a below succ.  All abstractions always
	// allow check and call.
	fprintf(stderr, "No below succ?!?\n");
	if (exit_on_error_) exit(-1);
	selected_succ = above_succ;
	if (debug_) {
	  fprintf(stderr, "Selected above bet (only); succ %i\n", above_succ);
	}
      } else {
	if (translate_to_larger_) {
	  selected_succ = above_succ;
	} else {
	  // Opponent's bet size is between two bets in our abstraction.
	  double below_prob = BelowProb(actual_opp_bet_to, below_bet_to,
					above_bet_to, actual_pot_size);
	  double r;
	  // r = RandZeroToOne();
	  drand48_r(&rand_bufs_[node->PlayerActing()], &r);
	  if (debug_) fprintf(stderr, "below_prob %f r %f\n", below_prob, r);
	  if (r < below_prob) {
	    if (debug_) {
	      fprintf(stderr, "Selected below bet; succ %i\n", below_succ);
	    }
	    selected_succ = below_succ;
	  } else {
	    if (debug_) {
	      fprintf(stderr, "Selected above bet; succ %i\n", above_succ);
	    }
	    selected_succ = above_succ;
	  }
	}
      }
      Node *bet_node = node->IthSucc(selected_succ);
      if (debug_) {
	fprintf(stderr, "Adding node st %u pa %u nt %u\n",
		bet_node->Street(),
		bet_node->PlayerActing(), bet_node->NonterminalID());
      }
      path_->push_back(bet_node);
    }
  }
}

// Does the right thing on the final street where 
unsigned int NLAgent::MSHCPIndex(unsigned int bd,
				 const Card *cards) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_board_cards = Game::NumBoardCards(max_street);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(max_street);
  const Card *board = cards + 2;
  unsigned int sg = BoardTree::SuitGroups(max_street, bd);
  CanonicalCards hands(2, board, num_board_cards, sg, false);
  hands.SortByHandStrength(board);
  for (unsigned int shcp = 0; shcp < num_hole_card_pairs; ++shcp) {
    const Card *hole_cards = hands.Cards(shcp);
    if (hole_cards[0] == cards[0] && hole_cards[1] == cards[1]) {
      return shcp;
    }
  }
  fprintf(stderr, "Didn't find cards in hands\n");
  exit(-1);
}

void NLAgent::UpdateCards(int street, Card our_hi, Card our_lo,
			  Card *raw_board, unsigned int *current_buckets,
			  unsigned int *bd) {
  if (debug_) fprintf(stderr, "UpdateCards %i\n", street);
  for (unsigned int st = 0; st <= (unsigned int)street; ++st) {
    if (st == 0) {
      Card cards[2];
      cards[0] = our_hi;
      cards[1] = our_lo;
      *bd = 0;
      // Assume 0 is not max street
      unsigned int hcp = HCPIndex(0, cards);
      if (buckets_->None(0)) {
	current_buckets[0] = hcp;
      } else {
	current_buckets[0] = buckets_->Bucket(0, hcp);
      }
    } else {
      unsigned int num_hole_cards = Game::NumCardsForStreet(0);
      unsigned int num_board_cards = Game::NumBoardCards(st);
      Card raw_hole_cards[2], canon_board[5], canon_hole_cards[2];
      raw_hole_cards[0] = our_hi;
      raw_hole_cards[1] = our_lo;
      CanonicalizeCards(raw_board, raw_hole_cards, st, canon_board,
			canon_hole_cards);
      *bd = BoardTree::LookupBoard(canon_board, st);
      // Put the hole cards at the beginning
      Card canon_cards[7];
      for (unsigned int i = 0; i < num_hole_cards; ++i) {
	canon_cards[i] = canon_hole_cards[i];
      }
      for (unsigned int i = 0; i < num_board_cards; ++i) {
	canon_cards[num_hole_cards + i] = canon_board[i];
      }
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int hcp, h;
      if (buckets_->None(st) || st >= endgame_st_) {
	if (st == Game::MaxStreet()) {
	  hcp = MSHCPIndex(*bd, canon_cards);
	} else {
	  hcp = HCPIndex(st, canon_cards);
	}
	h = *bd * num_hole_card_pairs + hcp;
	current_buckets[st] = h;
      } else {
	hcp = HCPIndex(st, canon_cards);
	h = *bd * num_hole_card_pairs + hcp;
	current_buckets[st] = buckets_->Bucket(st, h);
      }
#if 0
      // Would like this to go to stderr
      if (debug_) {
	OutputNCards(canon_board, num_board_cards);
	printf(" / ");
	OutputTwoCards(canon_hole_cards);
	printf(" bd %u hcp %u h %u b %u\n", *bd, hcp, h, current_buckets[st]);
	fflush(stdout);
      }
#endif
    }
  }
}

NLAgent::NLAgent(const CardAbstraction &base_ca,
		 const CardAbstraction &endgame_ca,
		 const BettingAbstraction &base_ba,
		 const BettingAbstraction &endgame_ba,
		 const CFRConfig &base_cc, const CFRConfig &endgame_cc,
		 const RuntimeConfig &rc, unsigned int *iterations,
		 BettingTree **betting_trees, unsigned int endgame_st,
		 unsigned int num_endgame_its, bool debug, bool exit_on_error,
		 bool fixed_seed, unsigned int small_blind,
		 unsigned int stack_size) :
  base_card_abstraction_(base_ca), endgame_card_abstraction_(endgame_ca),
  base_betting_abstraction_(base_ba), endgame_betting_abstraction_(endgame_ba),
  base_cfr_config_(base_cc), endgame_cfr_config_(endgame_cc),
  runtime_config_(rc) {
  unsigned int num_players = Game::NumPlayers();
  betting_trees_ = new BettingTree *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    betting_trees_[p] = betting_trees[p];
  }
  iterations_ = iterations;
  endgame_st_ = endgame_st;
  num_endgame_its_ = num_endgame_its;
  debug_ = debug;
  exit_on_error_ = exit_on_error;
  fixed_seed_ = fixed_seed;
  small_blind_ = small_blind;
  stack_size_ = stack_size;
  // Assume these are shared by P0 and P1
  respect_pot_frac_ = rc.RespectPotFrac();
  no_small_bets_ = rc.NoSmallBets();
  translation_method_ = rc.TranslationMethod();
  BoardTree::Create();
  BoardTree::CreateLookup();
  path_ = new vector<Node *>;

  // Need this for MSHCPIndex().
  HandValueTree::Create();
  buckets_ = new BucketsFile(base_ca);

  // In an asymmetric system, we need two probs_ objects because the
  // betting trees are different.  We also need both players' strategies for
  // each system, because of endgame solving.  (We need to calculate the
  // reach probabilities.)
  // In a symmetric system, we can create a single probs object with both
  // players' strategies.
  // We load all streets because we need the endgame probabilities for
  // computing the T-values.
  probs_ = new CFRValuesFile *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    unsigned int it = iterations_[p];
    const BettingTree *betting_tree = betting_trees_[p];
    const unsigned int *num_buckets = buckets_->NumBuckets();
    if (base_betting_abstraction_.Asymmetric()) {
      // Because of endgame solving we need both players' strategies for
      // each asym_p system.
      probs_[p] = new CFRValuesFile(nullptr, nullptr, base_ca, base_ba,
				    base_cc, p, it, endgame_st_, betting_tree,
				    num_buckets);
    } else {
      probs_[0] = new CFRValuesFile(nullptr, nullptr, base_ca, base_ba,
				    base_cc, p, it, endgame_st_, betting_tree,
				    num_buckets);
      for (unsigned int p1 = 1; p1 < num_players; ++p1) {
	probs_[p1] = probs_[0];
      }
    }
  }

  min_prob_ = rc.MinProb();
  fold_round_up_ = rc.FoldRoundUp();
  purify_ = rc.Purify();
  hard_coded_r200_strategy_ = rc.HardCodedR200Strategy();
  translate_to_larger_ = rc.TranslateToLarger();
  translate_bet_to_call_ = rc.TranslateBetToCall();

  endgame_buckets_.reset(new Buckets());
  dynamic_cbr_.reset(new DynamicCBR2(base_ca, base_ba, base_cc, *buckets_, 1));
  endgame_sumprobs_ = nullptr;
  endgame_subtree_ = nullptr;

  last_hand_index_ = kMaxUInt;
  folded_.reset(new bool[num_players]);

  rand_bufs_ = new drand48_data[num_players];
}

NLAgent::~NLAgent(void) {
  delete [] rand_bufs_;
  delete path_;
  delete endgame_sumprobs_;
  delete endgame_subtree_;
  if (base_betting_abstraction_.Asymmetric()) {
    unsigned int num_players = Game::NumPlayers();
    for (unsigned int p = 0; p < num_players; ++p) {
      delete probs_[p];
    }
  } else {
    delete probs_[0];
  }
  delete [] probs_;
  delete buckets_;
  delete [] betting_trees_;
  // Don't delete trees; we don't own them
}

// h can be either a bucket or a hole-card-pair index or a max-street
// hole-card-pair index.
double *NLAgent::CurrentProbs(Node *node, unsigned int h, unsigned int p) {
  // Arbitrary response if node is NULL.  Does this ever happen?
  if (node == NULL) {
    fprintf(stderr, "NLAgent::CurrentProbs() NULL node?!?\n");
    if (exit_on_error_) exit(-1);
    else                return NULL;
  }

  unsigned int num_succs = node->NumSuccs();
  unsigned int dsi = node->DefaultSuccIndex();
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  unsigned int num_players = Game::NumPlayers();
  if (num_players == 2) {
    // For reentrant trees, p may not be equal to pa.  I don't have a good
    // way to test for reentrant trees, so I just test for 2 vs. more than
    // 2 players, for now.
    if (p != pa) {
      fprintf(stderr, "CurrentProbs(): p != pa: p %u pa %u\n", p, pa);
      exit(-1);
    }
  }
  if (h == kMaxUInt) {
    fprintf(stderr, "Uninitialized bucket node %i street %i p%u\n",
	    node->NonterminalID(), st, pa);
    if (exit_on_error_) exit(-1);
    return NULL;
  }


  double *probs = new double[num_succs];
  // Expect the subgame to have been resolved if 1) we are on an endgame
  // street, and 2) num_succs > 1.  (2) is there because we don't bother to
  // resolve if we are already all-in.
  if (st >= endgame_st_ && num_succs > 1) {
    // We need the node from the endgame subtree.  We need the hand index,
    // not the bucket.
    if (endgame_sumprobs_ == nullptr) {
      fprintf(stderr, "No endgame sumprobs?!?\n");
      if (exit_on_error_) exit(-1);
      else                return nullptr;
    } else {
      if (debug_) {
	fprintf(stderr, "CurrentProbs(): node %u pa %u p %u street %u h %u\n",
		node->NonterminalID(), pa, p, st, h);
      }
      // Confusing: CFRValues objects take an offset argument,
      // while CFRValuesFile objects take a hand or bucket index.
      // h is a global hand index.  But we resolved an endgame for exactly
      // this board.  So we want the hcp_index.
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int hcp = h % num_hole_card_pairs;
      if (debug_) fprintf(stderr, "HCP %u\n", hcp);
      endgame_sumprobs_->Probs(p, st, node->NonterminalID(), hcp * num_succs,
			       num_succs, dsi, probs);
    }
  } else {
    if (debug_) {
      fprintf(stderr, "CurrentProbs(): node %u pa %u p %u street %i h %i\n",
	      node->NonterminalID(), pa, p, st, h);
    }
    // For multiplayer, pa may be different from p
    probs_[p]->Probs(pa, st, node->NonterminalID(), h, num_succs, dsi, probs);
  }
  return probs;
}

void NLAgent::AllProbs(Node *node, unsigned int s, unsigned int gbd,
		       CanonicalCards *hands, unsigned int p, double *probs) {
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  unsigned int dsi = node->DefaultSuccIndex();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int num_succs = node->NumSuccs();
  if (num_succs == 1) {
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      probs[i] = 1.0;
    }
    return;
  }
  unique_ptr<double []> hand_probs(new double[num_succs]);
  if (st >= endgame_st_ && num_succs > 1) {
  } else {
    // unsigned int max_card1 = Game::MaxCard() + 1;
    const Card *board = BoardTree::Board(st, gbd);
    unsigned int num_board_cards = Game::NumBoardCards(st);
    unique_ptr<Card []> cards(new Card[num_board_cards + 2]);
    for (unsigned int i = 0; i < num_board_cards; ++i) {
      cards[i+2] = board[i];
    }
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *hole_cards = hands->Cards(i);
      cards[0] = hole_cards[0];
      cards[1] = hole_cards[1];
      unsigned int holding;
      unsigned int hcp = HCPIndex(st, cards.get());
      unsigned int h = gbd * num_hole_card_pairs + hcp;
      if (buckets_->None(st)) {
	// This does wrong thing on river
	holding = h;
      } else {
	holding = buckets_->Bucket(st, h);
      }
      
      // For multiplayer, pa may be different from p
      probs_[p]->Probs(pa, st, nt, holding, num_succs, dsi, hand_probs.get());
      probs[i] = hand_probs[s];
    }
  }
}

// p should be the first candidate player.  If player 3 just acted, then
// pass in 4.  We will check if player 4 has not folded.
static unsigned int NextPlayer(unsigned int p, unsigned int num_players,
			       bool *folded) {
  unsigned int p1 = p;
  while (true) {
    if (p1 == num_players) p1 = 0;
    if (! folded[p1]) break;
    ++p1;
    if (p1 == p) {
      fprintf(stderr, "No possible next player?!?\n");
      exit(-1);
    }
  }
  return p1;
}

// Will this work for the blinds?
unsigned int NLAgent::LastBetSize(vector<Action> *actions) {
  unsigned int street = 0;
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_remaining = num_players;
  unsigned int num_to_act_on_street = num_players;
  unsigned int player_to_act = Game::FirstToAct(0);
  unique_ptr<bool []> folded(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    folded[p] = false;
  }
  unsigned int last_bet_to = 2 * small_blind_;
  unsigned int last_bet_size = small_blind_;
  unsigned int num_actions = actions->size();
  for (unsigned int i = 0; i < num_actions; ++i) {
    Action a = (*actions)[i];
    if (a.action_type == BET) {
      num_to_act_on_street = num_remaining - 1;
      player_to_act = NextPlayer(player_to_act + 1, num_players, folded.get());
      last_bet_size = a.bet_to - last_bet_to;
      last_bet_to = a.bet_to;
    } else if (a.action_type == FOLD) {
      folded[player_to_act] = true;
      --num_to_act_on_street;
      --num_remaining;
      if (num_remaining == 1) return 0;
      if (num_to_act_on_street == 0) {
	// Advance to next street
	num_to_act_on_street = num_remaining;
	++street;
	player_to_act = NextPlayer(Game::FirstToAct(street), num_players,
				   folded.get());
      } else {
	player_to_act = NextPlayer(player_to_act + 1, num_players,
				   folded.get());
      }
    } else {
      --num_to_act_on_street;
      if (num_to_act_on_street == 0) {
	// End of action on this street
	if (street == Game::MaxStreet()) {
	  // Showdown
	  return 0;
	} else {
	  // Advance to next street
	  num_to_act_on_street = num_remaining;
	  ++street;
	  player_to_act = NextPlayer(Game::FirstToAct(street), num_players,
				     folded.get());
	  last_bet_size = 0;
	}
      } else {
	// Action continues on this street
	player_to_act = NextPlayer(player_to_act + 1, num_players,
				   folded.get());
      }
    }
  }
  return last_bet_size;
}

// All-in in the *actual* game, not the abstract game.
bool NLAgent::AreWeAllIn(vector<Action> *actions, unsigned int p) {
  unsigned int street = 0;
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_remaining = num_players;
  unsigned int num_to_act_on_street = num_players;
  unsigned int player_to_act = Game::FirstToAct(0);
  unique_ptr<bool []> folded(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    folded[p] = false;
  }
  // Set to true when we see an all-in bet, and persists afterwards
  bool all_in_bet = false;
  unsigned int num_actions = actions->size();
  for (unsigned int i = 0; i < num_actions; ++i) {
    Action a = (*actions)[i];
    if (a.action_type == BET) {
      num_to_act_on_street = num_remaining - 1;
      if (debug_) {
	fprintf(stderr, "AreWeAllIn() saw bet to %u\n", a.bet_to);
      }
      if (a.bet_to == stack_size_) {
	if (debug_) fprintf(stderr, "Saw all-in bet\n");
	all_in_bet = true;
      }
      if (all_in_bet && player_to_act == p) return true;
      player_to_act = NextPlayer(player_to_act + 1, num_players, folded.get());
    } else if (a.action_type == FOLD) {
      folded[player_to_act] = true;
      --num_to_act_on_street;
      --num_remaining;
      if (num_remaining == 1) return false;
      if (num_to_act_on_street == 0) {
	// Advance to next street
	num_to_act_on_street = num_remaining;
	++street;
	player_to_act = NextPlayer(Game::FirstToAct(street), num_players,
				   folded.get());
      } else {
	player_to_act = NextPlayer(player_to_act + 1, num_players,
				   folded.get());
      }
    } else {
      // Call
      if (all_in_bet) {
	if (debug_) fprintf(stderr, "Saw call of all-in-bet\n");
	if (player_to_act == p) {
	  if (debug_) fprintf(stderr, "We called all-in-bet\n");
	  return true;
	}
      }
      --num_to_act_on_street;
      if (num_to_act_on_street == 0) {
	// End of action on this street
	if (street == Game::MaxStreet()) {
	  // Showdown
	  return false;
	} else {
	  // Advance to next street
	  num_to_act_on_street = num_remaining;
	  ++street;
	  player_to_act = NextPlayer(Game::FirstToAct(street), num_players,
				     folded.get());
	}
      } else {
	// Action continues on this street
	player_to_act = NextPlayer(player_to_act + 1, num_players,
				   folded.get());
      }
    }
  }
  return false;
}

unsigned int NLAgent::WhoseAction(vector<Action> *actions) {
  unsigned int street = 0;
  unsigned int num_players = Game::NumPlayers();
  unsigned int num_remaining = num_players;
  unsigned int num_to_act_on_street = num_players;
  unsigned int player_to_act = Game::FirstToAct(0);
  unique_ptr<bool []> folded(new bool[num_players]);
  for (unsigned int p = 0; p < num_players; ++p) {
    folded[p] = false;
  }
  unsigned int num_actions = actions->size();
  for (unsigned int i = 0; i < num_actions; ++i) {
    Action a = (*actions)[i];
    if (a.action_type == BET) {
      num_to_act_on_street = num_remaining - 1;
      player_to_act = NextPlayer(player_to_act + 1, num_players, folded.get());
    } else if (a.action_type == FOLD) {
      folded[player_to_act] = true;
      --num_to_act_on_street;
      --num_remaining;
      if (num_remaining == 1) return kMaxUInt;
      if (num_to_act_on_street == 0) {
	// Advance to next street
	num_to_act_on_street = num_remaining;
	++street;
	player_to_act = NextPlayer(Game::FirstToAct(street), num_players,
				   folded.get());
      } else {
	player_to_act = NextPlayer(player_to_act + 1, num_players,
				   folded.get());
      }
    } else {
      --num_to_act_on_street;
      if (num_to_act_on_street == 0) {
	// End of action on this street
	if (street == Game::MaxStreet()) {
	  // Showdown
	  return kMaxUInt;
	} else {
	  // Advance to next street
	  num_to_act_on_street = num_remaining;
	  ++street;
	  player_to_act = NextPlayer(Game::FirstToAct(street), num_players,
				     folded.get());
	}
      } else {
	// Action continues on this street
	player_to_act = NextPlayer(player_to_act + 1, num_players,
				   folded.get());
      }
    }
  }
  return player_to_act;
}

double **NLAgent::GetReachProbs(unsigned int current_bd, unsigned int asym_p) {
  unsigned int num_path = path_->size();
  if (num_path < 1) {
    fprintf(stderr, "Empty path?!?\n");
    exit(-1);
  }
  Node *current = (*path_)[num_path - 1];
  unsigned int current_st = current->Street();
  unsigned int num_players = Game::NumPlayers();
  double **reach_probs = new double *[num_players];
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  for (unsigned int p = 0; p < num_players; ++p) {
    reach_probs[p] = new double[num_enc];
    for (unsigned int i = 0; i < num_enc; ++i) {
      reach_probs[p][i] = 1.0;
    }
  }
  Card cards[7];
  const Card *board = BoardTree::Board(current_st, current_bd);
  unsigned int num_board_cards = Game::NumBoardCards(current_st);
  // Temporary?
  if (debug_) {
    OutputNCards(board, num_board_cards);
    printf("\n");
    fflush(stdout);
  }
  for (unsigned int i = 0; i < num_board_cards; ++i) {
    cards[i + 2] = board[i];
  }
  Card max_card = Game::MaxCard();
  for (unsigned int i = 0; i < num_path - 1; ++i) {
    Node *before = (*path_)[i];
    Node *after = (*path_)[i + 1];
    // This can happen when we map large bets up to an all-in.
    // For example, r19000c/cr19500c.  That last call doesn't correspond to
    // any succ in the abstract game.
    if (after == before) continue;
    unsigned int num_succs = before->NumSuccs();
    unsigned int s;
    for (s = 0; s < num_succs; ++s) {
      Node *n = before->IthSucc(s);
      if (n == after) break;
    }
    if (s == num_succs) {
      fprintf(stderr, "Couldn't connect nodes on path; i %u\n", i);
      fprintf(stderr, "Before st %u pa %u nt %u\n", before->Street(),
	      before->PlayerActing(), before->NonterminalID());
      fprintf(stderr, "After st %u pa %u nt %u\n", after->Street(),
	      after->PlayerActing(), after->NonterminalID());
      exit(-1);
    }
    unsigned int st = before->Street();
    if (buckets_->None(st)) {
      fprintf(stderr, "Expect buckets in GetReachProbs()\n");
      exit(-1);
    }
    if (st == Game::MaxStreet()) {
      fprintf(stderr, "Don't expect max-street nodes in GetReachProbs()\n");
      exit(-1);
    }
    unsigned int pa = before->PlayerActing();
    unsigned int nt = before->NonterminalID();
    unsigned int dsi = before->DefaultSuccIndex();
    unique_ptr<double []> probs(new double[num_succs]);
    unsigned int bd;
    if (st == current_st) {
      bd = current_bd;
    } else {
      bd = BoardTree::LookupBoard(board, st);
    }
    double sum = 0;
    for (Card hi = 1; hi <= max_card; ++hi) {
      if (InCards(hi, board, num_board_cards)) continue;
      cards[0] = hi;
      for (Card lo = 0; lo < hi; ++lo) {
	if (InCards(lo, board, num_board_cards)) continue;
	cards[1] = lo;
	unsigned int hcp = HCPIndex(st, cards);
	unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
	unsigned int h = bd * num_hole_card_pairs + hcp;
	unsigned int b = buckets_->Bucket(st, h);
	probs_[asym_p]->Probs(pa, st, nt, b, num_succs, dsi,
			      probs.get());
	unsigned int enc = hi * max_card1 + lo;
	reach_probs[pa][enc] *= probs[s];
	sum += reach_probs[pa][enc];
      }
    }
  }
  return reach_probs;
}

#if 0
static string ActionSequence(vector<Node *> *path) {
  unsigned int num_path = path->size();
  string action_sequence = "x";
  if (num_path <= 1) return action_sequence;
  for (unsigned int i = 0; i < num_path - 1; ++i) {
    Node *before = (*path)[i];
    Node *after = (*path)[i + 1];
    unsigned int num_succs = before->NumSuccs();
    unsigned int s;
    for (s = 0; s < num_succs; ++s) {
      Node *n = before->IthSucc(s);
      if (n == after) break;
    }
    if (s == num_succs) {
      fprintf(stderr, "Couldn't connect nodes on path\n");
      exit(-1);
    }
    if (s == before->CallSuccIndex()) {
      action_sequence += "c";
    } else if (s == before->FoldSuccIndex()) {
      action_sequence += "f";
    } else {
      unsigned int bet_size = after->LastBetTo() - before->LastBetTo();
      char buf[100];
      sprintf(buf, "b%u", bet_size);
      action_sequence += buf;
    }
  }
  return action_sequence;
}
#endif

// Go through previously processed actions and determine the actual bet-to
// amount of the last bet (by either ourselves or the opponent).
unsigned int NLAgent::GetLastActualBetTo(vector<Action> *actions) {
  unsigned int last_actual_bet_to = 2 * small_blind_;
  for (unsigned int i = 0; i < action_index_; ++i) {
    Action a = (*actions)[i];
    if (a.action_type == BET) last_actual_bet_to = a.bet_to;
  }
  return last_actual_bet_to;
}

// We keep track of the player to act, the current street and other things.
// We do *not* rely on the node in the tree to give us this information.  When
// we translate a bet to a call the node in the path may not accurately
// reflect whose action it really is.  Bets getting mapped up to an all-in
// also mess with us.
void NLAgent::ProcessActions(vector<Action> *actions, unsigned int we_p,
			     bool endgame, unsigned int *last_actual_bet_to,
			     Node **sob_node) {
  *sob_node = NULL;
  // Do I want to set all-in in this scenario?
  if (st_ == endgame_st_ && ! endgame) return;

  unsigned int num_players = Game::NumPlayers();
  unsigned int num_actions = actions->size();
  if (debug_) {
    fprintf(stderr, "ProcessActions action_index_ %u num_actions %u\n",
	    action_index_, num_actions);
  }
  while (action_index_ < num_actions) {
    Action a = (*actions)[action_index_];
    ++action_index_;
    Node *current_node = (*path_)[path_->size() - 1];
    if (current_node->Street() > st_ || current_node->Terminal()) {
      // This can happen when a bet gets mapped up to an all-in.
      // We don't want to do any translation now.  Just consume actions
      // until the streets match up.
      if (debug_) {
	fprintf(stderr, "Skipping translation because abstract street is "
		"ahead of actual street.\n");
      }
    } else {
      if (player_acting_ == we_p) {
	fprintf(stderr, "Shouldn't see our action; ai %u we_p %u pa %u\n",
		action_index_, we_p, player_acting_);
	exit(-1);
      }
      if (debug_) {
	fprintf(stderr, "Translating %i/%i\n", action_index_ - 1,
		num_actions);
      }
      Translate(a, sob_node, 2 * *last_actual_bet_to);
      if (debug_) fprintf(stderr, "Back from Translate()\n");
    }
    if (a.action_type == CALL) {
      --num_to_act_on_street_;
      if (num_to_act_on_street_ == 0) {
	if (st_ == Game::MaxStreet()) {
	  fprintf(stderr, "Shouldn't get to showdown in ProcessActions()\n");
	  exit(-1);
	}
	// Advance to next street
	num_to_act_on_street_ = num_remaining_;
	++st_;
	player_acting_ = NextPlayer(Game::FirstToAct(st_), num_players,
				   folded_.get());
      } else {
	// Action continues on this street
	player_acting_ = NextPlayer(player_acting_ + 1, num_players,
				    folded_.get());
      }
    } else if (a.action_type == BET) {
      *last_actual_bet_to = a.bet_to;
      num_to_act_on_street_ = num_remaining_ - 1;
      player_acting_ = NextPlayer(player_acting_ + 1, num_players,
				  folded_.get());
    } else if (a.action_type == FOLD) {
      folded_[player_acting_] = true;
      --num_to_act_on_street_;
      --num_remaining_;
      if (num_remaining_ == 1) {
	fprintf(stderr, "Shouldn't get to fold state in ProcessActions()\n");
	exit(-1);
      }
      if (num_to_act_on_street_ == 0) {
	// Advance to next street
	num_to_act_on_street_ = num_remaining_;
	++st_;
	player_acting_ = NextPlayer(Game::FirstToAct(st_), num_players,
				   folded_.get());
      } else {
	player_acting_ = NextPlayer(player_acting_ + 1, num_players,
				    folded_.get());
      }
    }
    if (st_ == endgame_st_ && ! endgame) {
      return;
    }
  }
}

// This is a special case when we have an opponent bet that is smaller than
// the smallest bet in our abstraction.  At this point we are undecided as
// to whether to map this bet to a check or to a bet.  We get the raise
// probs as if responding to the opponent's smallest bet.  We then draw a
// random number and decide if we are raising or not.  If we decide to
// raise, we replace the last node in the path (which was the opponent's
// check) with the smallest opponent bet node.
bool NLAgent::HandleRaise(Node *bet_node, unsigned int *current_buckets,
			  unsigned int p) {
  unsigned int num_succs = bet_node->NumSuccs();
  unsigned int fsi = bet_node->FoldSuccIndex();
  unsigned int csi = bet_node->CallSuccIndex();
  unsigned int st = bet_node->Street();
  unsigned int h = current_buckets[st];
  double *raw_probs = CurrentProbs(bet_node, h, p);
  double sum_raise_probs = 0;
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (s != fsi && s != csi) {
      sum_raise_probs += raw_probs[s];
    }
  }
  if (debug_) {
    fprintf(stderr, "HandleRaise: sum_raise_probs %f\n", sum_raise_probs);
  }
  delete [] raw_probs;
  double r;
  // r = RandZeroToOne();
  drand48_r(&rand_bufs_[p], &r);
  if (r < sum_raise_probs) {
    if (debug_) {
      fprintf(stderr, "HandleRaise: decided to raise\n");
      fprintf(stderr, "Replacing last node in path with st %u pa %u nt %u; "
	      "pos %i\n", bet_node->Street(), bet_node->PlayerActing(),
	      bet_node->NonterminalID(), (int)(path_->size() - 1));
      Node *previous = (*path_)[path_->size() - 1];
      fprintf(stderr, "Previously st %u pa %u nt %u\n", previous->Street(),
	      previous->PlayerActing(), previous->NonterminalID());
	      
    }
    // Replace the last node in the path with the bet node.
    (*path_)[path_->size() - 1] = bet_node;
    return true;
  } else {
    if (debug_) {
      fprintf(stderr, "HandleRaise: decided not to raise\n");
    }
    return false;
  }
}

// actual_opp_bet_to can't be different from last_actual_bet_to, can it?
double *NLAgent::GetActionProbs(const vector<Action> &actions,
				Node *sob_node, unsigned int *current_buckets,
				unsigned int p,	bool *force_call) {
  bool force_raise =
    (sob_node &&
     HandleRaise(sob_node, current_buckets, p));
  Node *current_node = (*path_)[path_->size() - 1];
  unsigned int num_succs = current_node->NumSuccs();
  unsigned int csi = current_node->CallSuccIndex();

  if (sob_node && ! force_raise) {
    // If we map a small bet down to a check/call *and* we decide not to
    // raise, we must force a call.  It wouldn't make sense to fold to a
    // size zero bet.
    // Keep in mind the current node might be a terminal (showdown) node.
    *force_call = true;
    if (debug_) {
      fprintf(stderr, "Not raising; forcing check/call\n");
    }
    return nullptr;
  }

  unsigned int fsi = current_node->FoldSuccIndex();
  unsigned int st = current_node->Street();

  unsigned int b = current_buckets[st];
  double *raw_probs = CurrentProbs(current_node, b, p);
  if (debug_) {
    fprintf(stderr, "raw_probs:");
    for (unsigned int s = 0; s < num_succs; ++s) {
      fprintf(stderr, " %f", raw_probs[s]);
    }
    fprintf(stderr, "\n");
  }

  double orig_call_prob = 0, fold_prob = 0;
  if (current_node->HasCallSucc()) orig_call_prob = raw_probs[csi];
  if (current_node->HasFoldSucc()) fold_prob = raw_probs[fsi];

  if (force_raise) {
    // Need to set call/fold probs to zero, scale up raise probs
    double sum_bet_probs = 1.0 - orig_call_prob - fold_prob;
    // We know sum_bet_probs > 0
    double scale_up = 1.0 / sum_bet_probs;
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (s == csi || s == fsi) raw_probs[s] = 0;
      else                      raw_probs[s] *= scale_up;
    }
    orig_call_prob = 0;
    fold_prob = 0;
    if (debug_) {
      fprintf(stderr, "modified raw_probs:");
      for (unsigned int s = 0; s < num_succs; ++s) {
	fprintf(stderr, " %f", raw_probs[s]);
      }
      fprintf(stderr, "\n");
    }
  }

  double *probs = new double[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) probs[s] = raw_probs[s];
  delete [] raw_probs;

  if (purify_) {
    double max_prob = -1;
    unsigned int max_s = 0;
    for (unsigned int s = 0; s < num_succs; ++s) {
      if (probs[s] > max_prob) {
	max_prob = probs[s];
	max_s = s;
      }
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      probs[s] = (s == max_s ? 1.0 : 0);
    }
  } else {
    if (fold_round_up_ > 0 && fsi < num_succs &&
	probs[fsi] >= fold_round_up_) {
      for (unsigned int s = 0; s < num_succs; ++s) {
	probs[s] = s == fsi ? 1.0 : 0;
      }
    } else {
      if (min_prob_ > 0) {
	double reassign_total = 0;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (probs[s] < min_prob_) reassign_total += probs[s];
	}
	if (reassign_total > 0 && reassign_total < 0.99) {
	  double rescaling = 1.0 / (1.0 - reassign_total);
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    if (probs[s] < min_prob_) probs[s] = 0;
	    else                      probs[s] *= rescaling;
	  }
	}
      }
    }
  }

  if (debug_) {
    for (unsigned int s = 0; s < num_succs; ++s) {
      fprintf(stderr, "s %u prob %f\n", s, probs[s]);
    }
  }
  return probs;
}

// This will force the new-hand logic to be performed upon the next call to
// HandleStateChange().
void NLAgent::SetNewHand(void) {
  last_hand_index_ = kMaxUInt;
}

BotAction NLAgent::HandleStateChange(const string &match_state,
				     unsigned int *we_bet_to) {
  if (debug_) {
    fprintf(stderr, "----------------------------------------\n");
    fprintf(stderr, "%s\n", match_state.c_str());
  }
  *we_bet_to = 0; // Default
  unsigned int p;
  unsigned int hand_index;
  unsigned int board_street;
  string action_str;
  Card our_hi, our_lo;
  Card board[5];
  unsigned int num_players = Game::NumPlayers();
  if (! ParseMatchState(match_state, num_players, &p, &hand_index, &action_str,
			&our_hi, &our_lo, board, &board_street)) {
    fprintf(stderr, "Couldn't parse match state message from server:\n");
    fprintf(stderr, "  %s\n", match_state.c_str());
    if (exit_on_error_) exit(-1);
    else                return BA_CALL;
  }
  if (debug_) fprintf(stderr, "P%u\n", p);
  if (hand_index != last_hand_index_) {
    path_->clear();
    path_->push_back(betting_trees_[p]->Root());
    action_index_ = 0;
    st_ = 0;
    player_acting_ = Game::FirstToAct(0);
    num_remaining_ = num_players;
    num_to_act_on_street_ = num_players;
    for (unsigned int p = 0; p < num_players; ++p) {
      folded_[p] = false;
    }
    delete endgame_sumprobs_;
    endgame_sumprobs_ = nullptr;
    delete endgame_subtree_;
    endgame_subtree_ = nullptr;
    last_hand_index_ = hand_index;
    if (fixed_seed_) {
      // Have a separate seed for each player.  Makes it easier to
      // match results from those of play.  May also prevent synchronization
      // which can lead to subtle biases.
      srand48_r(hand_index * num_players + p, &rand_bufs_[p]);
    }
  }
  if (debug_) {
    fprintf(stderr, "action_index_ %u\n", action_index_);
    fprintf(stderr, "st_ %u\n", st_);
  }

  unsigned int max_street = Game::MaxStreet();
  unique_ptr<unsigned int[]> current_buckets(new unsigned int[max_street + 1]);
  unsigned int bd;
  UpdateCards((int)board_street, our_hi, our_lo, board, current_buckets.get(),
	      &bd);
  if (debug_) {
    fprintf(stderr, "Action str: %s\n", action_str.c_str());
  }
  vector<Action> actions;
  ParseActions(action_str, false, exit_on_error_, &actions);

  unsigned int player_to_act = WhoseAction(&actions);
  if (debug_) fprintf(stderr, "player_to_act %u p %u\n", player_to_act, p);
  if (player_to_act == kMaxUInt) return BA_NONE;
  if (p != player_to_act) return BA_NONE;

  if (AreWeAllIn(&actions, p)) {
    if (debug_) fprintf(stderr, "We are all-in; returning no-action\n");
    *we_bet_to = 0;
    return BA_NONE;
  }
  
  Node *sob_node;
  bool endgame = endgame_sumprobs_ != nullptr;
  unsigned int last_actual_bet_to = GetLastActualBetTo(&actions);
  if (debug_) fprintf(stderr, "ProcessActions1\n");
  ProcessActions(&actions, p, endgame, &last_actual_bet_to, &sob_node);
  if (debug_) fprintf(stderr, "Back from ProcessActions1\n");
  if (debug_ && sob_node) {
    fprintf(stderr, "ProcessActions returned with sob node\n");
  }

  Node *current_node = (*path_)[path_->size() - 1];
  // This can happen when a not-all-in bet gets translated up to an all-in
  // bet.  We get to the terminal node, but we are not really all-in.
  // Should I return BA_NONE?
  if (current_node->Terminal()) {
    if (debug_) {
      fprintf(stderr, "At terminal node in abstract game; returning call\n");
    }
    return BA_CALL;
  }

  if (board_street >= endgame_st_ && endgame_sumprobs_ == nullptr) {
    // Don't resolve if current_node->NumSuccs() == 1.  This would be a case
    // where we are already all-in.  But we will still want to call
    // ProcessActions() a second time to process any remaining actions on
    // the river.
    if (current_node->NumSuccs() > 1) {
      if (debug_) fprintf(stderr, "Calling GetReachProbs()\n");
      double **reach_probs = GetReachProbs(bd, p);
      if (debug_) fprintf(stderr, "ResolveSubgame\n");
      ResolveSubgame(p, bd, reach_probs);
      if (debug_) fprintf(stderr, "Back from ResolveSubgame\n");
      for (unsigned int p = 0; p < num_players; ++p) {
	delete [] reach_probs[p];
      }
      delete [] reach_probs;
    }

    if (action_index_ != actions.size()) {
      // More actions by opponent to translate in the endgame.
      if (sob_node != nullptr) {
	fprintf(stderr, "sob_node already set pre-endgame?!?\n");
	exit(-1);
      }
      ProcessActions(&actions, p, true, &last_actual_bet_to, &sob_node);
    }
  }
  
  // Default
  BotAction bot_action = BA_NONE;
  Node *next_node = nullptr;

  current_node = (*path_)[path_->size() - 1];
  if (current_node->Street() != st_ || current_node->Terminal()) {
    // I have seen this in both heads-up and six-player.
    // In heads-up, we translated a large bet preflop to 19000 to an all-in.
    // On the turn, the action went cr19500c.  We translate the r19500 to
    // a check so after the "cr19500" the abstract betting state is already
    // on the river.  But the real-game action is still on the turn.

    if (debug_) {
      fprintf(stderr, "Forcing call because abstract street is ahead of "
	      "actual street\n");
    }
    bot_action = BA_CALL;
    // I think I do not want to advance the abstract betting state.
    // unsigned int csi = current_node->CallSuccIndex();
    // next_node = current_node->IthSucc(csi);
    next_node = current_node;
  } else {
    double *probs = NULL;

    bool force_call = false;
    probs = GetActionProbs(actions, sob_node, current_buckets.get(), p,
			   &force_call);
    // current_node can change inside GetActionProbs() because there is a
    // small bet that we originally mapped to call, but now, because we choose
    // to raise, we instead map to the smallest bet.
    current_node = (*path_)[path_->size() - 1];

    unsigned int num_succs = current_node->NumSuccs();
    unsigned int csi = current_node->CallSuccIndex();
    unsigned int fsi = current_node->FoldSuccIndex();

    if (force_call) {
      bot_action = BA_CALL;
      next_node = current_node->IthSucc(csi);
    } else {
      if (hard_coded_r200_strategy_ && path_->size() == 2 && p == 0 &&
	  current_node == betting_trees_[0]->Root()->IthSucc(2)) {
	unsigned int b = current_buckets[0];
	// Try r200 folding only 62o (17), 72o (26), 73o (28), 82o (37),
	// 83o (39), 92o (50).
	// Added: 32o (2), 93o (52), T2o (65), 42o (5), 52o (10), 63o (19),
	// T3o (67), 84o (41), J2o (82), 94o (54)
	if (b == 17 || b == 26 || b == 28 || b == 37 || b == 39 || b == 50 ||
	    b == 2 || b == 5 || b == 10 || b == 19 || b == 41 || b == 52 ||
	    b == 54 || b == 65 || b == 67 || b == 82) {
	  bot_action = BA_FOLD;
	  next_node = current_node->IthSucc(fsi);
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    probs[s] = (s == fsi ? 1.0 : 0);
	  }
	} else {
	  double fold_prob = probs[fsi];
	  if (fold_prob > 0) {
	    if (fold_prob > 0.9) {
	      for (unsigned int s = 0; s < num_succs; ++s) {
		probs[s] = (s == csi ? 1.0 : 0);
	      }
	    } else {
	      double scale = 1.0 / (1.0 - fold_prob);
	      for (unsigned int s = 0; s < num_succs; ++s) {
		probs[s] = (s == fsi ? 0 : probs[s] * scale);
	      }
	    }
	  }
	}
      }
      
      double r;
      r = RandZeroToOne();
      drand48_r(&rand_bufs_[p], &r);
      if (debug_) fprintf(stderr, "r %f\n", r);
      double cum = 0;
      if (num_succs == 0) {
	fprintf(stderr, "Can't handle zero succs\n");
	if (exit_on_error_) exit(-1);
      }
      unsigned int s;
      for (s = 0; s < num_succs - 1; ++s) {
	cum += probs[s];
	if (debug_) {
	  fprintf(stderr, "s %u prob %f cum %f\n", s, probs[s], cum);
	}
	if (r < cum) break;
      }
      delete [] probs;
      next_node = current_node->IthSucc(s);
      
      if (s == csi) {
	bot_action = BA_CALL;
      } else if (s == fsi) {
	bot_action = BA_FOLD;
      } else {
	bot_action = BA_BET;
	
	Node *b = current_node->IthSucc(s);
	*we_bet_to = b->LastBetTo() * small_blind_;
	// Note that our_bet_size can be negative
	int our_bet_size = (int)*we_bet_to - (int)last_actual_bet_to;
	if (debug_) {
	  fprintf(stderr, "s %i lbt %i wbt %u our_bet_size %i\n", s,
		  last_actual_bet_to, *we_bet_to, our_bet_size);
	}
	  
	// Make sure we bet at least one big blind.  (Note: in case that's
	// more than all-in, bet will be lowered below.)
	if (our_bet_size < (int)(2 * small_blind_)) {
	  our_bet_size = (int)(2 * small_blind_);
	  *we_bet_to = last_actual_bet_to + our_bet_size;
	}

	unsigned int last_bet_size = LastBetSize(&actions);
	// Make sure our raise is at least size of opponent's bet.  (Note: in
	// case that's more than all-in, bet will be lowered below.)
	if (our_bet_size < (int)last_bet_size) {
	  our_bet_size = last_bet_size;
	  *we_bet_to = last_actual_bet_to + our_bet_size;
	}
	
	// Make sure we are not betting more than all-in.
	if (*we_bet_to > stack_size_) {
	  *we_bet_to = stack_size_;
	  // Could be zero, in which case we should turn this bet into a call
	  our_bet_size = (int)*we_bet_to - (int)last_actual_bet_to;
	}
	
	// If bet size is zero, change action to a call.
	if (our_bet_size == 0) {
	  bot_action = BA_CALL;
	  *we_bet_to = 0;
	}
	  
	if (debug_) {
	  fprintf(stderr, "selected succ %u bet_to %u bet amount %u\n", s,
		  *we_bet_to, our_bet_size);
	}
      }
    }
  }
  ++action_index_;
  if (debug_) {
    fprintf(stderr, "Adding node st %u pa %u nt %u t %u\n",
	    next_node->Street(), next_node->PlayerActing(),
	    next_node->NonterminalID(), next_node->TerminalID());
  }
  path_->push_back(next_node);
  if (bot_action == BA_CALL) {
    --num_to_act_on_street_;
    if (num_to_act_on_street_ == 0) {
      if (st_ < Game::MaxStreet()) {
	num_to_act_on_street_ = num_remaining_;
	++st_;
	player_acting_ = NextPlayer(Game::FirstToAct(st_), num_players,
				    folded_.get());
      }
    } else {
      player_acting_ = NextPlayer(player_acting_ + 1, num_players,
				  folded_.get());
    }
  } else if (bot_action == BA_BET) {
    num_to_act_on_street_ = num_remaining_ - 1;
    player_acting_ = NextPlayer(player_acting_ + 1, num_players,
				folded_.get());
  } else if (bot_action == BA_FOLD) {
    folded_[player_acting_] = true;
    --num_to_act_on_street_;
    --num_remaining_;
    if (num_to_act_on_street_ == 0) {
      num_to_act_on_street_ = num_remaining_;
      ++st_;
      player_acting_ = NextPlayer(Game::FirstToAct(st_), num_players,
				  folded_.get());
    } else {
      player_acting_ = NextPlayer(player_acting_ + 1, num_players,
				  folded_.get());
    }
  }
  return bot_action;
}
