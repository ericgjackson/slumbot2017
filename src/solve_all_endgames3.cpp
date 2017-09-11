// Like solve_all_endgames2, but we always solve a complete street
// subgame.
//
// What should the merged system consist of?
//   Should the street-initial node only allow one *future* bet size?
//   This seems a lot like the progressive endgame solving that worked
//   poorly with solve_all_endgames2.
//   But maybe it's a bit different in that we will always back up and
//   solve the entire street when resolving at an internal node.
//   Will we have a problem with inconsistent ranges?  Probably.
// Write out strategy at each node as in solve_all_endgames2.
// Allow overwriting, again just like solve_all_endgames2.
//
// Write() need to only write out the part of the strategy below the
// "branch point".

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree_builder.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_utils.h"
#include "dynamic_cbr.h"
#include "eg_cfr.h"
#include "endgame_utils.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"
#include "vcfr.h"

class EndgameSolver3 {
public:
  EndgameSolver3(const CardAbstraction &base_card_abstraction,
		 const CardAbstraction &endgame_card_abstraction,
		 const BettingAbstraction &base_betting_abstraction,
		 const BettingAbstraction &endgame_betting_abstraction,
		 const CFRConfig &base_cfr_config,
		 const CFRConfig &endgame_cfr_config,
		 const Buckets &base_buckets, const Buckets &endgame_buckets,
		 unsigned int solve_st, ResolvingMethod method,
		 unsigned int base_it, unsigned int num_endgame_its);
  ~EndgameSolver3(void) {}
  void Walk(unsigned int target_p);
private:
  BettingTree *CreateBaseSubtree(Node *node, unsigned int num_street_bets,
				 unsigned int target_p);
  BettingTree *CreateSubtree(Node *street_root, Node *branch_point,
			     unsigned int num_street_bets,
			     unsigned int target_p);
  void StreetInitial(Node *node, const string &action_sequence,
		     unsigned int pgbd, double **reach_probs,
		     const CFRValues *sumprobs, unsigned int root_bd_st,
		     unsigned int root_bd, HandTree *hand_tree,
		     const Buckets &buckets,
		     const CardAbstraction &card_abstraction,
		     bool in_resolve_region, unsigned int target_p);
  void InitialResolve(Node *node, unsigned int gbd,
		      const string &action_sequence, double **reach_probs,
		      unsigned int num_street_bets, unsigned int target_p);
  void Walk(Node *node, const string &action_sequence, Node *street_root,
	    const string &street_root_action_sequence, unsigned int gbd,
	    double **reach_probs, unsigned int num_street_bets,
	    bool in_resolve_region, const CFRValues *sumprobs,
	    unsigned int root_bd_st,unsigned int root_bd, HandTree *hand_tree,
	    const Buckets &buckets, const CardAbstraction &card_abstraction,
	    unsigned int target_p, unsigned int last_st);

  const CardAbstraction &base_card_abstraction_;
  const CardAbstraction &endgame_card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const BettingAbstraction &endgame_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
  const CFRConfig &endgame_cfr_config_;
  const Buckets &base_buckets_;
  const Buckets &endgame_buckets_;
  unique_ptr<BettingTree> base_betting_tree_;
  unique_ptr<CFRValues> trunk_sumprobs_;
  unsigned int solve_st_;
  ResolvingMethod method_;
  unsigned int base_it_;
  unsigned int num_endgame_its_;
};

EndgameSolver3::EndgameSolver3(const CardAbstraction &base_card_abstraction,
			       const CardAbstraction &endgame_card_abstraction,
			       const BettingAbstraction &
			       base_betting_abstraction,
			       const BettingAbstraction &
			       endgame_betting_abstraction,
			       const CFRConfig &base_cfr_config,
			       const CFRConfig &endgame_cfr_config,
			       const Buckets &base_buckets,
			       const Buckets &endgame_buckets,
			       unsigned int solve_st, ResolvingMethod method,
			       unsigned int base_it,
			       unsigned int num_endgame_its) :
  base_card_abstraction_(base_card_abstraction),
  endgame_card_abstraction_(endgame_card_abstraction),
  base_betting_abstraction_(base_betting_abstraction),
  endgame_betting_abstraction_(endgame_betting_abstraction),
  base_cfr_config_(base_cfr_config), endgame_cfr_config_(endgame_cfr_config),
  base_buckets_(base_buckets), endgame_buckets_(endgame_buckets) {
  solve_st_ = solve_st;
  method_ = method;
  base_it_ = base_it;
  num_endgame_its_ = num_endgame_its;
  base_betting_tree_.reset(BettingTree::BuildTree(base_betting_abstraction_));

  // We need probs for both players
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> trunk_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    trunk_streets[st] = st < solve_st_;
  }
  unique_ptr<bool []> compressed_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    compressed_streets[st] = false;
  }
  const vector<unsigned int> &csv = base_cfr_config_.CompressedStreets();
  unsigned int num_csv = csv.size();
  for (unsigned int i = 0; i < num_csv; ++i) {
    unsigned int st = csv[i];
    compressed_streets[st] = true;
  }
  trunk_sumprobs_.reset(new CFRValues(nullptr, true, trunk_streets.get(),
				      base_betting_tree_.get(), 0, 0,
				      base_card_abstraction_, base_buckets_,
				      compressed_streets.get()));

  char dir[500];
  sprintf(dir, "%s/%s.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(),
	  base_card_abstraction_.CardAbstractionName().c_str(),
	  Game::NumRanks(),
	  Game::NumSuits(), Game::MaxStreet(),
	  base_betting_abstraction_.BettingAbstractionName().c_str(),
	  base_cfr_config_.CFRConfigName().c_str());
  if (base_betting_abstraction_.Asymmetric()) {
    // Maybe move trunk_sumprob initialization to inside of loop over
    // target players.
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
  }
#if 0
  // How do I handle this?
  if (base_betting_abstraction_.Asymmetric()) {
    char buf[100];
    sprintf(buf, ".p%u", target_p_);
    strcat(dir, buf);
  }
#endif
  trunk_sumprobs_->Read(dir, base_it_, base_betting_tree_->Root(),
			base_betting_tree_->Root()->NonterminalID(), kMaxUInt);
}

static Node *FindCorrespondingNode(Node *old_node, Node *old_target,
				   Node *new_node) {
  if (old_node->Terminal()) return nullptr;
  if (old_node == old_target) return new_node;
  unsigned int num_succs = old_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Node *new_target = FindCorrespondingNode(old_node->IthSucc(s), old_target,
					     new_node->IthSucc(s));
    if (new_target) return new_target;
  }
  return nullptr;
}

// Assume no bet pending
BettingTree *EndgameSolver3::CreateBaseSubtree(Node *node,
					       unsigned int num_street_bets,
					       unsigned int target_p) {
  unsigned int player_acting = node->PlayerActing();
  // Wait, should pot size include pending bet?
  unsigned int pot_size = node->PotSize();
  unsigned int st = node->Street();
  // Don't have good way to compute last_bet_size!!!
  unsigned int last_bet_size = 0;
  unsigned int num_terminals = 0;
  // Only need initial street, stack size and min bet from
  // base_betting_abstraction_.
  BettingTreeBuilder betting_tree_builder(base_betting_abstraction_,
					  target_p);
  Node *subtree_root =
    betting_tree_builder.CreateNoLimitTree1(st, pot_size, last_bet_size,
					    num_street_bets, player_acting,
					    target_p, &num_terminals);
  // Delete the nodes under subtree_root?
  return BettingTree::BuildSubtree(subtree_root);
}

BettingTree *EndgameSolver3::CreateSubtree(Node *street_root,
					   Node *branch_point,
					   unsigned int num_street_bets,
					   unsigned int target_p) {
  unsigned int player_acting = branch_point->PlayerActing();
  // Wait, should pot size include pending bet?
  unsigned int pot_size;
  if (num_street_bets > 0) {
    pot_size = branch_point->IthSucc(branch_point->CallSuccIndex())->PotSize();
  } else {
    pot_size = branch_point->PotSize();
  }
  unsigned int st = branch_point->Street();
  // Only need initial street, stack size and min bet from
  // base_betting_abstraction_.
  BettingTreeBuilder betting_tree_builder(base_betting_abstraction_,
					  target_p);

  bool our_bet = (target_p == player_acting);
  const vector<double> *base_pot_fracs = nullptr;
  const vector<double> *endgame_pot_fracs = nullptr;
  unsigned int num_base_pot_fracs = 0, num_endgame_pot_fracs = 0;
  if (num_street_bets < base_betting_abstraction_.MaxBets(st, our_bet)) {
    base_pot_fracs =
      base_betting_abstraction_.BetSizes(st, num_street_bets, our_bet);
    num_base_pot_fracs = base_pot_fracs->size();
  }
  if (num_street_bets < endgame_betting_abstraction_.MaxBets(st, our_bet)) {
    endgame_pot_fracs =
      endgame_betting_abstraction_.BetSizes(st, num_street_bets, our_bet);
    num_endgame_pot_fracs = endgame_pot_fracs->size();
  }
  if (num_base_pot_fracs == num_endgame_pot_fracs) {
    return nullptr;
  } else if (num_base_pot_fracs != num_endgame_pot_fracs - 1) {
    fprintf(stderr, "Endgame betting abstraction should offer the same "
	    "number of bet sizes, or one more bet size, than the base "
	    "betting abstraction\n");
    fprintf(stderr, "num_base %u num_endgame %u\n", num_base_pot_fracs,
	    num_endgame_pot_fracs);
    exit(-1);
  }
  double frac = 0;
  for (unsigned int i = 0; i < num_endgame_pot_fracs; ++i) {
    frac = (*endgame_pot_fracs)[i];
    unsigned int j;
    for (j = 0; j < num_base_pot_fracs; ++j) {
      if ((*base_pot_fracs)[j] == frac) break;
    }
    if (j == num_base_pot_fracs) break;
  }
  double double_new_bet_size = pot_size * frac;
  unsigned int new_bet_size = (unsigned int)(double_new_bet_size + 0.5);

  unsigned int num_terminals = 0;
  Node *subtree_root =
    betting_tree_builder.BuildAugmented(street_root, branch_point,
					new_bet_size, 0, target_p,
					&num_terminals);
  // Delete the nodes under subtree_root?
  return BettingTree::BuildSubtree(subtree_root);
}

void EndgameSolver3::StreetInitial(Node *node, const string &action_sequence,
				   unsigned int pgbd, double **reach_probs,
				   const CFRValues *sumprobs,
				   unsigned int root_bd_st,
				   unsigned int root_bd, HandTree *hand_tree,
				   const Buckets &buckets,
				   const CardAbstraction &card_abstraction,
				   bool in_resolve_region,
				   unsigned int target_p) {
  unsigned int nst = node->Street();
  unsigned int pst = node->Street() - 1;
  unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
  unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
  for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
    Walk(node, action_sequence, node, action_sequence, ngbd, reach_probs, 0,
	 in_resolve_region, sumprobs, root_bd_st, root_bd, hand_tree, buckets,
	 card_abstraction, target_p, nst);
  }
}

// Currently assume that this is a street-initial node.
void EndgameSolver3::InitialResolve(Node *node, unsigned int gbd,
				    const string &action_sequence,
				    double **reach_probs,
				    unsigned int num_street_bets,
				    unsigned int target_p) {
  BettingTree *subtree = CreateBaseSubtree(node, num_street_bets, target_p);
  CFRValues *new_sumprobs =
    ReadBaseEndgameStrategy(base_card_abstraction_, base_betting_abstraction_,
			    base_cfr_config_, base_betting_tree_.get(),
			    base_buckets_, endgame_buckets_, base_it_, node,
			    gbd, action_sequence, reach_probs, subtree,
			    target_p);
  unsigned int st = node->Street();
  HandTree new_hand_tree(st, gbd, Game::MaxStreet());
  Walk(subtree->Root(), action_sequence, subtree->Root(), action_sequence, gbd,
       reach_probs, num_street_bets, true, new_sumprobs, st, gbd,
       &new_hand_tree, base_buckets_, base_card_abstraction_, target_p, st);
  delete subtree;
  delete new_sumprobs;
}


void EndgameSolver3::Walk(Node *node, const string &action_sequence,
			  Node *street_root,
			  const string &street_root_action_sequence, 
			  unsigned int gbd,
			  double **reach_probs,
			  unsigned int num_street_bets, bool in_resolve_region,
			  const CFRValues *sumprobs, unsigned int root_bd_st,
			  unsigned int root_bd, HandTree *hand_tree,
			  const Buckets &buckets,
			  const CardAbstraction &card_abstraction,
			  unsigned int target_p, unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    StreetInitial(node, action_sequence, gbd, reach_probs, sumprobs,
		  root_bd_st, root_bd, hand_tree, buckets, card_abstraction,
		  in_resolve_region, target_p);
    return;
  }
  bool new_in_resolve_region = in_resolve_region || (st == solve_st_);
  if (new_in_resolve_region && ! in_resolve_region) {
    // Do we assume that this is a street-initial node?
    // We do assume no bet pending
    InitialResolve(node, gbd, action_sequence, reach_probs, num_street_bets,
		   target_p);
    return;
  }
  
  Node *new_node = node;
  Node *new_street_root = street_root;
  const CFRValues *new_sumprobs = sumprobs;
  unsigned int new_root_bd_st = root_bd_st;
  unsigned int new_root_bd = root_bd;
  HandTree *new_hand_tree = hand_tree;
  const Buckets &new_buckets = buckets;
  const CardAbstraction &new_card_abstraction = card_abstraction;
  
  bool may_resolve = new_in_resolve_region;
  // For now, resolve at all nodes
  // node->PlayerActing() != target_p);

  BettingTree *subtree = nullptr;
  bool resolve = false;
  if (may_resolve) {
    subtree = CreateSubtree(street_root, node, num_street_bets, target_p);
    if (subtree) {
      resolve = true;
      new_street_root = subtree->Root();
      new_node = FindCorrespondingNode(street_root, node, new_street_root);
      if (new_node == nullptr) {
	fprintf(stderr, "Couldn't find corresponding\n");
	exit(-1);
      }
      printf("gbd %u\n", gbd);
      subtree->Display();
      fflush(stdout);
    }
  }
  if (resolve) {
    // Pass in false for both players.  I am doing separate solves for
    // each player.
    for (unsigned int cfr_target_p = 0; cfr_target_p <= 1; ++cfr_target_p) {
      DynamicCBR dynamic_cbr;
      double *t_vals = dynamic_cbr.Compute(node, reach_probs, gbd, hand_tree,
					   sumprobs, root_bd_st, root_bd,
					   buckets, card_abstraction,
					   cfr_target_p^1, false);
      bool cfrs = false;
      bool zero_sum = true;
      EGCFR eg_cfr(endgame_card_abstraction_, base_card_abstraction_,
		   endgame_betting_abstraction_, base_betting_abstraction_,
		   endgame_cfr_config_, base_cfr_config_, endgame_buckets_,
		   st, method_, cfrs, zero_sum, 1);
      eg_cfr.SolveSubgame(subtree, gbd, reach_probs, new_hand_tree, t_vals,
			  cfr_target_p, false, num_endgame_its_);
      delete [] t_vals;
	      
      WriteEndgame(subtree->Root(), street_root_action_sequence,
		   action_sequence, gbd, base_card_abstraction_,
		   endgame_card_abstraction_, base_betting_abstraction_,
		   endgame_betting_abstraction_, base_cfr_config_,
		   endgame_cfr_config_, method_, eg_cfr.Sumprobs(), st, gbd,
		   target_p, cfr_target_p, st);
    }

    // This is a little wasteful.  We wrote the sumprobs out inside
    // Resolve().  Now we read them back in.  new_sumprobs now has the resolved
    // strategy for the subgame.  Nonterminal indices are those from subtree.
    // This reads the strategy for the entire street subtree.  We only need
    // the portion of the strategy below the current node, but oh well.
    new_sumprobs = ReadEndgame(street_root_action_sequence, subtree, gbd,
			       base_card_abstraction_,
			       endgame_card_abstraction_,
			       base_betting_abstraction_,
			       endgame_betting_abstraction_,
			       base_cfr_config_, endgame_cfr_config_,
			       endgame_buckets_, method_, st, gbd,  target_p);
    new_root_bd_st = st;
    new_root_bd = gbd;
  }

  double ***succ_reach_probs =
    GetSuccReachProbs(new_node, gbd, new_hand_tree, new_buckets, new_sumprobs,
		      reach_probs, new_in_resolve_region);
  
  unsigned int num_succs = new_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = new_node->ActionName(s);
    unsigned int new_num_street_bets;
    if (s == new_node->CallSuccIndex() || s == new_node->FoldSuccIndex()) {
      new_num_street_bets = num_street_bets;
    } else {
      new_num_street_bets = num_street_bets + 1;
    }
    Walk(new_node->IthSucc(s), action_sequence + action, new_street_root,
	 street_root_action_sequence, gbd, succ_reach_probs[s],
	 new_num_street_bets, new_in_resolve_region, new_sumprobs,
	 new_root_bd_st, new_root_bd, new_hand_tree, new_buckets,
	 new_card_abstraction, target_p, st);
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    for (unsigned int p = 0; p < 2; ++p) {
      delete [] succ_reach_probs[s][p];
    }
    delete [] succ_reach_probs[s];
  }
  delete [] succ_reach_probs;
}

void EndgameSolver3::Walk(unsigned int target_p) {
  unsigned int num_players = Game::NumPlayers();
  double **reach_probs = new double *[num_players];
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  HandTree preflop_hand_tree(0, 0, 0);
  const CanonicalCards *preflop_hands = preflop_hand_tree.Hands(0, 0);
  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(0);
  for (unsigned int p = 0; p < num_players; ++p) {
    reach_probs[p] = new double[num_enc];
    for (unsigned int i = 0; i < num_enc; ++i) reach_probs[p][i] = 0;
    for (unsigned int i = 0; i < num_hole_card_pairs; ++i) {
      const Card *cards = preflop_hands->Cards(i);
      unsigned int enc = cards[0] * max_card1 + cards[1];
      reach_probs[p][enc] = 1.0;
    }
  }
  Walk(base_betting_tree_->Root(), "", base_betting_tree_->Root(), "", 0, 
       reach_probs, 0, false, trunk_sumprobs_.get(), 0, 0, &preflop_hand_tree,
       base_buckets_, base_card_abstraction_, target_p, 0);
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card params> "
	  "<endgame card params> <base betting params> "
	  "<endgame betting params> <base CFR params> <endgame CFR params> "
	  "<solve street> <base it> <num endgame its> "
	  "[unsafe|cfrd|maxmargin|combined]\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 12) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> base_card_params = CreateCardAbstractionParams();
  base_card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    base_card_abstraction(new CardAbstraction(*base_card_params));
  unique_ptr<Params> endgame_card_params = CreateCardAbstractionParams();
  endgame_card_params->ReadFromFile(argv[3]);
  unique_ptr<CardAbstraction>
    endgame_card_abstraction(new CardAbstraction(*endgame_card_params));
  unique_ptr<Params> base_betting_params = CreateBettingAbstractionParams();
  base_betting_params->ReadFromFile(argv[4]);
  unique_ptr<BettingAbstraction>
    base_betting_abstraction(new BettingAbstraction(*base_betting_params));
  unique_ptr<Params> endgame_betting_params = CreateBettingAbstractionParams();
  endgame_betting_params->ReadFromFile(argv[5]);
  unique_ptr<BettingAbstraction>endgame_betting_abstraction(
		   new BettingAbstraction(*endgame_betting_params));
  unique_ptr<Params> base_cfr_params = CreateCFRParams();
  base_cfr_params->ReadFromFile(argv[6]);
  unique_ptr<CFRConfig> base_cfr_config(new CFRConfig(*base_cfr_params));
  unique_ptr<Params> endgame_cfr_params = CreateCFRParams();
  endgame_cfr_params->ReadFromFile(argv[7]);
  unique_ptr<CFRConfig> endgame_cfr_config(new CFRConfig(*endgame_cfr_params));
  unsigned int solve_st, base_it, num_endgame_its;
  if (sscanf(argv[8], "%u", &solve_st) != 1)         Usage(argv[0]);
  if (sscanf(argv[9], "%u", &base_it) != 1)         Usage(argv[0]);
  if (sscanf(argv[10], "%u", &num_endgame_its) != 1) Usage(argv[0]);
  string m = argv[11];
  ResolvingMethod method;
  if (m == "unsafe")         method = ResolvingMethod::UNSAFE;
  else if (m == "cfrd")      method = ResolvingMethod::CFRD;
  else if (m == "maxmargin") method = ResolvingMethod::MAXMARGIN;
  else if (m == "combined")  method = ResolvingMethod::COMBINED;
  else                       Usage(argv[0]);

  Buckets base_buckets(*base_card_abstraction, false);
  Buckets endgame_buckets(*endgame_card_abstraction, false);

  BoardTree::Create();

  EndgameSolver3 solver(*base_card_abstraction, *endgame_card_abstraction,
			*base_betting_abstraction,
			*endgame_betting_abstraction, *base_cfr_config,
			*endgame_cfr_config, base_buckets, endgame_buckets,
			solve_st, method, base_it, num_endgame_its);
  for (unsigned int target_p = 0; target_p <= 1; ++target_p) {
    DeleteAllEndgames(*base_card_abstraction, *endgame_card_abstraction,
		      *base_betting_abstraction, *endgame_betting_abstraction,
		      *base_cfr_config, *endgame_cfr_config, method, target_p);
    solver.Walk(target_p);
  }
}
