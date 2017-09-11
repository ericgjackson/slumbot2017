#ifndef _ENDGAME_UTILS_H_
#define _ENDGAME_UTILS_H_

#include <string>

#include "eg_cfr.h" // ResolvingMethod

using namespace std;

class BettingAbstraction;
class BettingTree;
class Buckets;
class CardAbstraction;
class CFRConfig;
class CFRValues;
class HandTree;
class Node;

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
				   unsigned int target_p);
double ***GetSuccReachProbs(Node *node, unsigned int gbd, HandTree *hand_tree,
			    const Buckets &buckets, const CFRValues *sumprobs,
			    double **reach_probs, bool in_resolve_region);
void DeleteAllEndgames(const CardAbstraction &base_card_abstraction,
		       const CardAbstraction &endgame_card_abstraction,
		       const BettingAbstraction &base_betting_abstraction,
		       const BettingAbstraction &endgame_betting_abstraction,
		       const CFRConfig &base_cfr_config,
		       const CFRConfig &endgame_cfr_config,
		       ResolvingMethod method, unsigned int target_p);
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
		  unsigned int last_st);
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
		       unsigned int root_bd, unsigned int target_p);

#endif
