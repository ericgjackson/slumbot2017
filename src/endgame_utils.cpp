#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_values.h"
#include "eg_cfr.h" // ResolvingMethod
#include "endgame_utils.h"
#include "files.h"
#include "game.h"
#include "hand_tree.h"
#include "io.h"

using namespace std;

// I always load probabilities for both players because I need the reach
// probabilities for both players.  In addition, as long as we are
// zero-summing the T-values, we need the strategy for both players for the
// CBR computation.
CFRValues *ReadBaseEndgameStrategy(const CardAbstraction &
				   base_card_abstraction,
				   const BettingAbstraction &
				   base_betting_abstraction,
				   const CFRConfig &base_cfr_config,
				   const BettingTree *base_betting_tree,
				   const Buckets &base_buckets,
				   const Buckets &endgame_buckets,
				   unsigned int base_it,
				   Node *base_node, unsigned int gbd,
				   const string &action_sequence,
				   double **reach_probs,
				   BettingTree *subtree,
				   unsigned int target_p) {

  // We need probs for subgame streets only
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> subgame_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    subgame_streets[st] = st >= base_node->Street();
  }
  CFRValues *sumprobs = new CFRValues(nullptr, true, subgame_streets.get(),
				      base_betting_tree, gbd,
				      base_node->Street(),
				      base_card_abstraction, base_buckets,
				      nullptr);

  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction.BettingAbstractionName().c_str(),
	  base_cfr_config.CFRConfigName().c_str());
  if (base_betting_abstraction.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", target_p);
    strcat(dir, buf);
  }

  unique_ptr<unsigned int []>
    num_full_holdings(new unsigned int[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (endgame_buckets.None(st)) {
      num_full_holdings[st] =
	BoardTree::NumBoards(st) * Game::NumHoleCardPairs(st);
    } else {
      num_full_holdings[st] = endgame_buckets.NumBuckets(st);
    }
  }

  sumprobs->ReadSubtreeFromFull(dir, base_it, base_betting_tree->Root(),
				base_node, subtree->Root(), action_sequence,
				num_full_holdings.get(), kMaxUInt);
  
  return sumprobs;
}

double ***GetSuccReachProbs(Node *node, unsigned int gbd, HandTree *hand_tree,
			    const Buckets &buckets, const CFRValues *sumprobs,
			    double **reach_probs, bool in_resolve_region) {
  unsigned int num_succs = node->NumSuccs();
  double ***succ_reach_probs = new double **[num_succs];
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
  if (nt >= sumprobs->NumNonterminals(pa, st)) {
    fprintf(stderr, "GetSuccReachProbs: OOB nt %u numnt %u\n", nt,
	    sumprobs->NumNonterminals(pa, st));
    exit(-1);
  }
  unsigned int dsi = node->DefaultSuccIndex();
  // Can we assume a local board index of 0?  I guess so.  Because we always
  // resolve at street-initial nodes and we will create a new hand tree
  // (just for the current global board) when we resolve.
  const CanonicalCards *hands = hand_tree->Hands(st, 0);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    // If we're in the trunk, then we want the global board index to
    // access the probs.  If we're resolving, then we have a sumprobs
    // object customized to the current board, so use a local board index
    // of zero.
    unsigned int lbd = in_resolve_region ? 0 : gbd;
    unsigned int offset;
    if (buckets.None(st)) {
      offset = lbd * num_hole_card_pairs * num_succs + i * num_succs;
    } else {
      unsigned int h = lbd * num_hole_card_pairs + i;
      unsigned int b = buckets.Bucket(st, h);
      offset = b * num_succs;
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      double prob = sumprobs->Prob(pa, st, nt, offset, s, num_succs, dsi);
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

void DeleteAllEndgames(const CardAbstraction &base_card_abstraction,
		       const CardAbstraction &endgame_card_abstraction,
		       const BettingAbstraction &base_betting_abstraction,
		       const BettingAbstraction &endgame_betting_abstraction,
		       const CFRConfig &base_cfr_config,
		       const CFRConfig &endgame_cfr_config,
		       ResolvingMethod method, unsigned int target_p) {
  char dir[500], dir2[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction.BettingAbstractionName().c_str(),
	  base_cfr_config.CFRConfigName().c_str());
  if (base_betting_abstraction.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", target_p);
    strcat(dir, buf);
  }
  for (unsigned int cfr_target_p = 0; cfr_target_p <= 1; ++cfr_target_p) {
    sprintf(dir2, "%s/endgames.%s.%s.%s.%s.p%u.p%u", dir,
	    endgame_card_abstraction.CardAbstractionName().c_str(),
	    endgame_betting_abstraction.BettingAbstractionName().c_str(),
	    endgame_cfr_config.CFRConfigName().c_str(),
	    ResolvingMethodName(method), target_p, cfr_target_p);
    if (FileExists(dir2)) {
      fprintf(stderr, "Recursively deleting %s\n", dir2);
      RecursivelyDeleteDirectory(dir2);
    }
  }
}

// Only write out strategy for nodes at or below below_action_sequence.
void WriteEndgame(Node *node, const string &action_sequence,
		  const string &below_action_sequence, unsigned int gbd,
		  const CardAbstraction &base_card_abstraction,
		  const CardAbstraction &endgame_card_abstraction,
		  const BettingAbstraction &base_betting_abstraction,
		  const BettingAbstraction &endgame_betting_abstraction,
		  const CFRConfig &base_cfr_config,
		  const CFRConfig &endgame_cfr_config, ResolvingMethod method,
		  const CFRValues *sumprobs,
		  unsigned int root_bd_st, unsigned int root_bd,
		  unsigned int target_p, unsigned int cfr_target_p,
		  unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(last_st, gbd, st);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(last_st, gbd, st);
    for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      WriteEndgame(node, action_sequence, below_action_sequence, ngbd,
		   base_card_abstraction, endgame_card_abstraction,
		   base_betting_abstraction, endgame_betting_abstraction,
		   base_cfr_config, endgame_cfr_config, method, sumprobs,
		   root_bd_st, root_bd, target_p, cfr_target_p, st);
    }
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  if (node->PlayerActing() == cfr_target_p && num_succs > 1) {
    if (below_action_sequence.size() <= action_sequence.size() &&
	std::equal(below_action_sequence.begin(), below_action_sequence.end(),
		   action_sequence.begin())) {
      char dir[500], dir2[500], dir3[500], filename[500];
      sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	      Game::GameName().c_str(),
	      base_card_abstraction.CardAbstractionName().c_str(),
	      Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	      base_betting_abstraction.BettingAbstractionName().c_str(),
	      base_cfr_config.CFRConfigName().c_str());
      if (base_betting_abstraction.Asymmetric()) {
	char buf[100];
	sprintf(buf, ".p%u", target_p);
	strcat(dir, buf);
      }
      sprintf(dir2, "%s/endgames.%s.%s.%s.%s.p%u.p%u", dir,
	      endgame_card_abstraction.CardAbstractionName().c_str(),
	      endgame_betting_abstraction.BettingAbstractionName().c_str(),
	      endgame_cfr_config.CFRConfigName().c_str(),
	      ResolvingMethodName(method), target_p, cfr_target_p);
      Mkdir(dir2);
    
      if (action_sequence == "") {
	fprintf(stderr, "Empty action sequence not allowed\n");
	exit(-1);
      }
      sprintf(dir3, "%s/%s", dir2, action_sequence.c_str());
      Mkdir(dir3);

      sprintf(filename, "%s/%u", dir3, gbd);

      Writer writer(filename);
      // Need to apply an offset when writing out values at a node that is
      // on a later street than the root of the subgame.
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(node->Street());
      unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
      unsigned int offset = lbd * num_hole_card_pairs * num_succs;
      sumprobs->WriteNode(node, &writer, nullptr, num_hole_card_pairs, offset);
    }
  }

  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    WriteEndgame(node->IthSucc(s), action_sequence + action,
		 below_action_sequence, gbd,
		 base_card_abstraction, endgame_card_abstraction,
		 base_betting_abstraction, endgame_betting_abstraction,
		 base_cfr_config, endgame_cfr_config, method, sumprobs,
		 root_bd_st, root_bd, target_p, cfr_target_p, st);
  }
}

static void ReadEndgame(Node *node, const string &action_sequence,
			unsigned int gbd,
			const CardAbstraction &base_card_abstraction,
			const CardAbstraction &endgame_card_abstraction,
			const BettingAbstraction &base_betting_abstraction,
			const BettingAbstraction &endgame_betting_abstraction,
			const CFRConfig &base_cfr_config,
			const CFRConfig &endgame_cfr_config,
			ResolvingMethod method,	CFRValues *sumprobs,
			unsigned int root_bd_st, unsigned int root_bd,
			unsigned int target_p, unsigned int cfr_target_p,
			unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(last_st, gbd, st);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(last_st, gbd, st);
    for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      ReadEndgame(node, action_sequence, ngbd, base_card_abstraction,
		  endgame_card_abstraction, base_betting_abstraction,
		  endgame_betting_abstraction, base_cfr_config,
		  endgame_cfr_config, method, sumprobs, root_bd_st, root_bd,
		  target_p, cfr_target_p, st);
    }
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  if (node->PlayerActing() == cfr_target_p && num_succs > 1) {
    char dir[500], dir2[500], dir3[500], filename[500];
    sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	    Game::GameName().c_str(),
	    base_card_abstraction.CardAbstractionName().c_str(),
	    Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	    base_betting_abstraction.BettingAbstractionName().c_str(),
	    base_cfr_config.CFRConfigName().c_str());
    if (base_betting_abstraction.Asymmetric()) {
      char buf[100];
      sprintf(buf, ".p%u", target_p);
      strcat(dir, buf);
    }
    sprintf(dir2, "%s/endgames.%s.%s.%s.%s.p%u.p%u", dir,
	    endgame_card_abstraction.CardAbstractionName().c_str(),
	    endgame_betting_abstraction.BettingAbstractionName().c_str(),
	    endgame_cfr_config.CFRConfigName().c_str(),
	    ResolvingMethodName(method), target_p, cfr_target_p);
    
    if (action_sequence == "") {
      fprintf(stderr, "Empty action sequence not allowed\n");
      exit(-1);
    }
    sprintf(dir3, "%s/%s", dir2, action_sequence.c_str());

    sprintf(filename, "%s/%u", dir3, gbd);

    Reader reader(filename);
    // Assume doubles in file
    // Also assume endgame solving is unabstracted
    // We write only one board's data per file, even on streets later than
    // solve street.
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(node->Street());
    unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
    unsigned int offset = lbd * num_hole_card_pairs * num_succs;
    sumprobs->ReadNode(node, &reader, nullptr, num_hole_card_pairs, false,
		       offset);
  }

  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    ReadEndgame(node->IthSucc(s), action_sequence + action, gbd,
		base_card_abstraction, endgame_card_abstraction,
		base_betting_abstraction, endgame_betting_abstraction,
		base_cfr_config, endgame_cfr_config, method, sumprobs,
		root_bd_st, root_bd, target_p, cfr_target_p, st);
  }
}

// I always load probabilities for both players because I need the reach
// probabilities for both players in case we are performing nested endgame
// solving.  In addition, as long as we are zero-summing the T-values, we
// need the strategy for both players for the CBR computation.
CFRValues *ReadEndgame(const string &action_sequence,
		       BettingTree *subtree, unsigned int gbd,
		       const CardAbstraction &base_card_abstraction,
		       const CardAbstraction &endgame_card_abstraction,
		       const BettingAbstraction &base_betting_abstraction,
		       const BettingAbstraction &endgame_betting_abstraction,
		       const CFRConfig &base_cfr_config,
		       const CFRConfig &endgame_cfr_config,
		       const Buckets &endgame_buckets,
		       ResolvingMethod method, unsigned int root_bd_st,
		       unsigned int root_bd, unsigned int target_p) {
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> subgame_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    subgame_streets[st] = st >= subtree->Root()->Street();
  }

  // Card abstraction needed only for bucket thresholds
  // Buckets needed for num buckets
  // If we solve endgames with no card abstraction, maybe it doesn't matter
  CFRValues *sumprobs = new CFRValues(nullptr, true,
				      subgame_streets.get(),
				      subtree, gbd, subtree->Root()->Street(),
				      endgame_card_abstraction,
				      endgame_buckets, nullptr);

  for (unsigned int cfr_target_p = 0; cfr_target_p <= 1; ++cfr_target_p) {
    ReadEndgame(subtree->Root(), action_sequence, gbd, base_card_abstraction,
		endgame_card_abstraction, base_betting_abstraction,
		endgame_betting_abstraction, base_cfr_config,
		endgame_cfr_config, method, sumprobs, root_bd_st, root_bd,
		target_p, cfr_target_p, subtree->Root()->Street());
  }
  
  return sumprobs;
}
