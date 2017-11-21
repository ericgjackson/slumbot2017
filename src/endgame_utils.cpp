// There are up to four endgame strategies written per solve, which can be
// confusing.  If we are solving an asymmetric game, then there will be
// a choice of which player's game we are solving, which affects the betting
// tree.  In addition, regardless of whether we are solving a symmetric or
// asymmetric game, we have to write out both player's strategies in the
// endgame.  We refer to the first player parameter as the asym_p and the
// second player parameter as the target_pa.

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
#include "cfr_utils.h"
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
// Actually: for normal endgame solving, I think I only need both players'
// strategies if we are zero-summing.
CFRValues *ReadBaseEndgameStrategy(const CardAbstraction &
				   base_card_abstraction,
				   const BettingAbstraction &
				   base_betting_abstraction,
				   const CFRConfig &base_cfr_config,
				   const BettingTree *base_betting_tree,
				   const Buckets &base_buckets,
				   const Buckets &endgame_buckets,
				   unsigned int base_it, Node *base_node,
				   unsigned int gbd,
				   const string &action_sequence,
				   double **reach_probs, BettingTree *subtree,
				   bool current, unsigned int asym_p) {

  // We need probs for subgame streets only
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> subgame_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    subgame_streets[st] = st >= base_node->Street();
  }
  CFRValues *strategy = new CFRValues(nullptr, ! current,
				      subgame_streets.get(), base_betting_tree,
				      gbd, base_node->Street(),
				      base_card_abstraction, base_buckets,
				      nullptr);

  char dir[500];
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  base_card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction.BettingAbstractionName().c_str(),
	  base_cfr_config.CFRConfigName().c_str());
  if (base_betting_abstraction.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", asym_p);
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

  strategy->ReadSubtreeFromFull(dir, base_it, base_betting_tree->Root(),
				base_node, subtree->Root(), action_sequence,
				num_full_holdings.get(), kMaxUInt);
  
  return strategy;
}

double ***GetSuccReachProbs(Node *node, unsigned int gbd, HandTree *hand_tree,
			    const Buckets &buckets, const CFRValues *sumprobs,
			    double **reach_probs, unsigned int root_bd_st,
			    unsigned int root_bd, bool purify) {
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
  unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
  const CanonicalCards *hands = hand_tree->Hands(st, lbd);
  unique_ptr<double []> probs(new double [num_succs]);
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *cards = hands->Cards(i);
    Card hi = cards[0];
    Card lo = cards[1];
    unsigned int enc = hi * max_card1 + lo;
    unsigned int offset;
    if (buckets.None(st)) {
      offset = lbd * num_hole_card_pairs * num_succs + i * num_succs;
    } else {
      unsigned int h = lbd * num_hole_card_pairs + i;
      unsigned int b = buckets.Bucket(st, h);
      offset = b * num_succs;
    }
    if (purify) {
      if (sumprobs->Ints(pa, st)) {
	int *i_values;
	sumprobs->Values(pa, st, nt, &i_values);
	PureProbs(i_values, num_succs, probs.get());
      } else if (sumprobs->Doubles(pa, st)) {
	double *d_values;
	sumprobs->Values(pa, st, nt, &d_values);
	PureProbs(d_values, num_succs, probs.get());
      } else {
	fprintf(stderr, "Expected int or double sumprobs\n");
	exit(-1);
      }
    } else {
      for (unsigned int s = 0; s < num_succs; ++s) {
	probs[s] = sumprobs->Prob(pa, st, nt, offset, s, num_succs, dsi);
      }
    }
    for (unsigned int s = 0; s < num_succs; ++s) {
      double prob = probs[s];
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
		       ResolvingMethod method, unsigned int asym_p) {
  char dir[500], dir2[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction.BettingAbstractionName().c_str(),
	  base_cfr_config.CFRConfigName().c_str());
  if (base_betting_abstraction.Asymmetric()) {
    for (unsigned int target_pa = 0; target_pa <= 1; ++target_pa) {
      sprintf(dir2, "%s.p%u/endgames.%s.%s.%s.%s.p%u.p%u", dir, asym_p,
	      endgame_card_abstraction.CardAbstractionName().c_str(),
	      endgame_betting_abstraction.BettingAbstractionName().c_str(),
	      endgame_cfr_config.CFRConfigName().c_str(),
	      ResolvingMethodName(method), asym_p, target_pa);
      if (FileExists(dir2)) {
	fprintf(stderr, "Recursively deleting %s\n", dir2);
	RecursivelyDeleteDirectory(dir2);
      }
    }
  } else {
    for (unsigned int target_pa = 0; target_pa <= 1; ++target_pa) {
      sprintf(dir2, "%s/endgames.%s.%s.%s.%s.p%u", dir,
	      endgame_card_abstraction.CardAbstractionName().c_str(),
	      endgame_betting_abstraction.BettingAbstractionName().c_str(),
	      endgame_cfr_config.CFRConfigName().c_str(),
	      ResolvingMethodName(method), target_pa);
      if (FileExists(dir2)) {
	fprintf(stderr, "Recursively deleting %s\n", dir2);
	RecursivelyDeleteDirectory(dir2);
      }
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
		  unsigned int asym_p, unsigned int target_pa,
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
		   root_bd_st, root_bd, asym_p, target_pa, st);
    }
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  if (node->PlayerActing() == target_pa && num_succs > 1) {
    if (below_action_sequence.size() <= action_sequence.size() &&
	std::equal(below_action_sequence.begin(), below_action_sequence.end(),
		   action_sequence.begin())) {
      char dir[500], dir2[500], dir3[500], filename[500];
      sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	      Game::GameName().c_str(), Game::NumPlayers(),
	      base_card_abstraction.CardAbstractionName().c_str(),
	      Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	      base_betting_abstraction.BettingAbstractionName().c_str(),
	      base_cfr_config.CFRConfigName().c_str());
      if (base_betting_abstraction.Asymmetric()) {
	sprintf(dir2, "%s.p%u/endgames.%s.%s.%s.%s.p%u.p%u", dir, asym_p,
		endgame_card_abstraction.CardAbstractionName().c_str(),
		endgame_betting_abstraction.BettingAbstractionName().c_str(),
		endgame_cfr_config.CFRConfigName().c_str(),
		ResolvingMethodName(method), asym_p, target_pa);
      } else {
	sprintf(dir2, "%s/endgames.%s.%s.%s.%s.p%u", dir, 
		endgame_card_abstraction.CardAbstractionName().c_str(),
		endgame_betting_abstraction.BettingAbstractionName().c_str(),
		endgame_cfr_config.CFRConfigName().c_str(),
		ResolvingMethodName(method), target_pa);
      }
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
		 root_bd_st, root_bd, asym_p, target_pa, st);
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
			unsigned int asym_p, unsigned int target_pa,
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
		  asym_p, target_pa, st);
    }
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  if (node->PlayerActing() == target_pa && num_succs > 1) {
    char dir[500], dir2[500], dir3[500], filename[500];
    sprintf(dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	    Game::GameName().c_str(), Game::NumPlayers(),
	    base_card_abstraction.CardAbstractionName().c_str(),
	    Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	    base_betting_abstraction.BettingAbstractionName().c_str(),
	    base_cfr_config.CFRConfigName().c_str());
    if (base_betting_abstraction.Asymmetric()) {
      sprintf(dir2, "%s.p%u/endgames.%s.%s.%s.%s.p%u.p%u", dir, asym_p,
	    endgame_card_abstraction.CardAbstractionName().c_str(),
	    endgame_betting_abstraction.BettingAbstractionName().c_str(),
	    endgame_cfr_config.CFRConfigName().c_str(),
	    ResolvingMethodName(method), asym_p, target_pa);
    } else {
      sprintf(dir2, "%s/endgames.%s.%s.%s.%s.p%u", dir, 
	    endgame_card_abstraction.CardAbstractionName().c_str(),
	    endgame_betting_abstraction.BettingAbstractionName().c_str(),
	    endgame_cfr_config.CFRConfigName().c_str(),
	    ResolvingMethodName(method), target_pa);
    }
    
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
    sumprobs->ReadNode(node, &reader, nullptr, num_hole_card_pairs, CFR_DOUBLE,
		       offset);
  }

  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    ReadEndgame(node->IthSucc(s), action_sequence + action, gbd,
		base_card_abstraction, endgame_card_abstraction,
		base_betting_abstraction, endgame_betting_abstraction,
		base_cfr_config, endgame_cfr_config, method, sumprobs,
		root_bd_st, root_bd, asym_p, target_pa, st);
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
		       unsigned int root_bd, unsigned int asym_p) {
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

  for (unsigned int target_pa = 0; target_pa <= 1; ++target_pa) {
    ReadEndgame(subtree->Root(), action_sequence, gbd, base_card_abstraction,
		endgame_card_abstraction, base_betting_abstraction,
		endgame_betting_abstraction, base_cfr_config,
		endgame_cfr_config, method, sumprobs, root_bd_st, root_bd,
		asym_p, target_pa, subtree->Root()->Street());
  }
  
  return sumprobs;
}

void FloorCVs(Node *subtree_root, double *opp_reach_probs,
	      const CanonicalCards *hands, double *cvs) {
  unsigned int st = subtree_root->Street();
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
  unsigned int maxcard1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    double sum_opp_reach_probs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
      sum_opp_reach_probs += opp_reach_probs[opp_enc];
    }
    double our_norm_cv = cvs[i] / sum_opp_reach_probs;
    if (our_norm_cv < -(double)subtree_root->LastBetTo()) {
      cvs[i] = (-(double)subtree_root->LastBetTo()) * sum_opp_reach_probs;
    }
  }
  
}

void CalculateMeanCVs(double *p0_cvs, double *p1_cvs,
		      unsigned int num_hole_card_pairs, double **reach_probs,
		      const CanonicalCards *hands, double *p0_mean_cv,
		      double *p1_mean_cv) {
  unsigned int maxcard1 = Game::MaxCard() + 1;
  double sum_p0_cvs = 0, sum_p1_cvs = 0, sum_joint_probs = 0;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    unsigned int our_enc = our_hi * maxcard1 + our_lo;
    double sum_p0_opp_probs = 0;
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
      sum_p0_opp_probs += reach_probs[0][opp_enc];
    }
    double p0_prob = reach_probs[0][our_enc];
    double p1_prob = reach_probs[1][our_enc];
    sum_p0_cvs += p0_cvs[i] * p0_prob;
    sum_p1_cvs += p1_cvs[i] * p1_prob;
    sum_joint_probs += p1_prob * sum_p0_opp_probs;
#if 0
    if (p0_cvs[i] > 10000 || p0_cvs[i] < -10000) {
      fprintf(stderr, "OOB p0 cv i %u cv %f\n", i, p0_cvs[i]);
      exit(-1);
    }
    if (p1_cvs[i] > 10000 || p1_cvs[i] < -10000) {
      fprintf(stderr, "OOB p1 cv i %u cv %f\n", i, p1_cvs[i]);
      exit(-1);
    }
#endif
  }
  *p0_mean_cv = sum_p0_cvs / sum_joint_probs;
  *p1_mean_cv = sum_p1_cvs / sum_joint_probs;
}

void ZeroSumCVs(double *p0_cvs, double *p1_cvs,
		unsigned int num_hole_card_pairs, double **reach_probs,
		const CanonicalCards *hands) {
  double p0_mean_cv, p1_mean_cv;
  CalculateMeanCVs(p0_cvs, p1_cvs, num_hole_card_pairs, reach_probs, hands,
		   &p0_mean_cv, &p1_mean_cv);
  // fprintf(stderr, "Mean CVs: %f, %f\n", p0_mean_cv, p1_mean_cv);

  double avg = (p0_mean_cv + p1_mean_cv) / 2.0;
  double adj = -avg;
  unsigned int maxcard1 = Game::MaxCard() + 1;
  for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
    const Card *our_cards = hands->Cards(i);
    Card our_hi = our_cards[0];
    Card our_lo = our_cards[1];
    double sum_p0_opp_probs = 0, sum_p1_opp_probs = 0;    
    for (unsigned int j = 0; j < num_hole_card_pairs; ++j) {
      const Card *opp_cards = hands->Cards(j);
      Card opp_hi = opp_cards[0];
      Card opp_lo = opp_cards[1];
      if (opp_hi == our_hi || opp_hi == our_lo || opp_lo == our_hi ||
	  opp_lo == our_lo) {
	continue;
      }
      unsigned int opp_enc = opp_hi * maxcard1 + opp_lo;
      sum_p0_opp_probs += reach_probs[0][opp_enc];
      sum_p1_opp_probs += reach_probs[1][opp_enc];
    }
    p0_cvs[i] += adj * sum_p1_opp_probs;
    p1_cvs[i] += adj * sum_p0_opp_probs;
  }

  // I can take this out
  double adj_p0_mean_cv, adj_p1_mean_cv;
  CalculateMeanCVs(p0_cvs, p1_cvs, num_hole_card_pairs, reach_probs, hands,
		   &adj_p0_mean_cv, &adj_p1_mean_cv);
#if 0
  fprintf(stderr, "Adj mean CVs: P0 %f, P1 %f\n", adj_p0_mean_cv,
	  adj_p1_mean_cv);
#endif
  
}
