// This is a new program for solving many endgames in a strategy.
// After running solve_all_endgames2, you will typically run
// assemble_endgame2 to create a new merged strategy that combines the trunk
// strategy from the base with all the resolved endgames.  You can then
// evaluate this merged strategy however you like; perhaps with run_rgbr.
//
// This program is supposed to have a couple of advantages over
// solve_all_endgames:
// 1) It's supposed to mimic more closely what we will do in the
// competition.  So we can start evaluating the "real" code, and even
// create code that we can eventually use in the final bot.
// 2) It's supposed to enable subgame resolving that is rooted at a betting
// state that is not street-initial.
// 3) It's supposed to enable progressive (nested) endgame resolving.
// 4) It's supposed to clean some things up.  For example, we don't rely on
// nonterminal IDs for storing strategies on disk.  This was always a mess
// because different betting trees (e.g., the base tree and the endgame
// tree) have different nonterminal IDs, and we always had to be careful
// to use the right ones.
//
// With respect to (1), one main difference in solve_all_endgames2 is that
// we compute card-level CBRs on the fly, instead of relying on CBR values
// that have been computed offline by build_cbrs.  This is what we will need
// to do for the competition because storage (and maybe computation)
// requirements would be too much to precompute them.
//
// There are three boolean flags that control how we do the endgame solving:
// 1) Nested: if false, we just solve the endgame when we get to the resolve
// street and then return.  If true, we also resolved nested endgames within
// the first resolve.
// 2) Internal: if false, then we only do nested resolves at street-initial
// nodes.  If true, we do it at every target player choice node.
// 3) Progressive: if true, we construct betting trees that allow more
// choices one step ahead.
//
// I read the base strategy from disk for the endgame streets.  I wonder if
// this will be too slow for the current way that we store our strategies.
// Since our base will be bucketed we will likely need to read through the
// entire river strategy to find the parts that we care about.  That does not
// seem tenable.  Probably will need to break sumprobs into different files for
// each subtree.
//
// This may be tricky to do optimally because I don't know in advance what
// the start points of endgames will be.  But it might be sufficient if
// every turn subtree has its own file.
//
// This code is much slower than solve_all_endgames on ms2f1t1/mb1b1.  Is
// it due to CBR calculation?  Will this go away on real games of interest?

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

class EndgameSolver2 {
public:
  EndgameSolver2(const CardAbstraction &base_ca,
		 const CardAbstraction &endgame_ca,
		 const BettingAbstraction &base_ba,
		 const BettingAbstraction &endgame_ba,
		 const CFRConfig &base_cc, const CFRConfig &endgame_cc,
		 const Buckets &base_buckets, const Buckets &endgame_buckets,
		 unsigned int base_it, unsigned int solve_st,
		 ResolvingMethod method, bool cfrs, bool zero_sum,
		 unsigned int num_its, bool nested, bool internal,
		 bool progressive);
  ~EndgameSolver2(void);
  void Walk(unsigned int solve_st, unsigned int target_p);
private:
  BettingTree *CreateSubtree(Node *root, bool base_tree,
			     unsigned int num_street_bets,
			     unsigned int target_p);
  void StreetInitial(Node *node, unsigned int pbd, unsigned int solve_st,
		     const string &action_sequence, double **reach_probs,
		     CFRValues *sumprobs, unsigned int root_bd_st,
		     unsigned int root_bd, HandTree *hand_tree,
		     const Buckets &buckets,
		     const CardAbstraction &card_abstraction, bool resolving,
		     unsigned int target_p);
  void InitialResolve(Node *node, unsigned int gbd, unsigned int solve_st,
		      const string &action_sequence, double **reach_probs,
		      unsigned int num_street_bets, unsigned int target_p);
  void Walk(Node *node, unsigned int bd, unsigned int solve_st,
	    const string &action_sequence, double **reach_probs,
	    unsigned int num_street_bets, bool resolving,
	    CFRValues *sumprobs, unsigned int root_bd_st, unsigned int root_bd,
	    HandTree *hand_tree, const Buckets &buckets,
	    const CardAbstraction &card_abstraction, unsigned int target_p,
	    unsigned int last_st);
  
  const CardAbstraction &base_card_abstraction_;
  const CardAbstraction &endgame_card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const BettingAbstraction &endgame_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
  const CFRConfig &endgame_cfr_config_;
  const Buckets &base_buckets_;
  const Buckets &endgame_buckets_;
  unsigned int base_it_;
  ResolvingMethod method_;
  bool cfrs_;
  bool zero_sum_;
  unsigned int num_endgame_its_;
  bool nested_;
  bool internal_;
  bool progressive_;
  BettingTree *base_betting_tree_;
  unique_ptr<CFRValues> trunk_sumprobs_;
  unsigned int **street_buckets_;
};

// Aren't we leaking all the nodes returned by CreateNoLimitTree4()?
// BuildSubtree() clones that tree.
BettingTree *EndgameSolver2::CreateSubtree(Node *root, bool base_tree,
					   unsigned int num_street_bets,
					   unsigned int target_p) {
  unsigned int player_acting = root->PlayerActing();
  unsigned int pot_size = root->PotSize();
  unsigned int last_bet_size;
  if (root->HasCallSucc()) {
    unsigned int csi = root->CallSuccIndex();
    Node *c = root->IthSucc(csi);
    last_bet_size = (c->PotSize() - pot_size) / 2;
  }
  unsigned int st = root->Street();
  unsigned int terminal_id = 0;
  // Only need initial street, stack size and min bet from
  // base_betting_abstraction_.
  BettingTreeBuilder betting_tree_builder(base_betting_abstraction_,
					  target_p);

  const BettingAbstraction &immediate_betting_abstraction =
    base_tree ? base_betting_abstraction_ : endgame_betting_abstraction_;
  const BettingAbstraction &future_betting_abstraction =
    base_tree ? base_betting_abstraction_ :
    (progressive_ ? base_betting_abstraction_ :
     endgame_betting_abstraction_);
  Node *subtree_root =
    betting_tree_builder.CreateNoLimitTree4(st, pot_size, last_bet_size,
					    num_street_bets, player_acting,
					    target_p,
					    immediate_betting_abstraction,
					    future_betting_abstraction,
					    &terminal_id);
  return BettingTree::BuildSubtree(subtree_root);
}

void EndgameSolver2::StreetInitial(Node *node, unsigned int pgbd,
				   unsigned int solve_st,
				   const string &action_sequence,
				   double **reach_probs, CFRValues *sumprobs,
				   unsigned int root_bd_st,
				   unsigned int root_bd, HandTree *hand_tree,
				   const Buckets &buckets,
				   const CardAbstraction &card_abstraction,
				   bool resolving, unsigned int target_p) {
  unsigned int nst = node->Street();
  unsigned int pst = node->Street() - 1;
  unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
  unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
  for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
    Walk(node, ngbd, solve_st, action_sequence, reach_probs, 0, resolving,
	 sumprobs, root_bd_st, root_bd, hand_tree, buckets, card_abstraction,
	 target_p, nst);
  }
}

void EndgameSolver2::InitialResolve(Node *node, unsigned int gbd,
				    unsigned int solve_st,
				    const string &action_sequence,
				    double **reach_probs,
				    unsigned int num_street_bets,
				    unsigned int target_p) {
  BettingTree *subtree = CreateSubtree(node, true, num_street_bets, target_p);
  CFRValues *new_sumprobs =
    ReadBaseEndgameStrategy(base_card_abstraction_, base_betting_abstraction_,
			    base_cfr_config_, base_betting_tree_,
			    base_buckets_, endgame_buckets_, base_it_, node,
			    gbd, action_sequence, reach_probs, subtree,
			    target_p);
  unsigned int st = node->Street();
  HandTree new_hand_tree(st, gbd, Game::MaxStreet());
  Walk(subtree->Root(), gbd, solve_st, action_sequence, reach_probs,
       num_street_bets, true, new_sumprobs, st, gbd, &new_hand_tree,
       base_buckets_, base_card_abstraction_, target_p, st);
  delete subtree;
  delete new_sumprobs;
}

// sumprobs is the last-computed strategy.  During the trunk, it is
// trunk_sumprobs_.  When we get to the first resolve node, we reset
// sumprobs inside Walk() (we read the endgame strategy for this subtree from
// the base).  Once we start resolving, sumprobs is the strategy computed
// by the last resolve.
// node corresponds to the betting tree used to compute sumprobs.  So it
// is the trunk betting tree in the trunk.
// sumprobs, root_bd_st, root_bd and hand_tree should all correspond to the
// last computed strategy.  They may be rooted at a street prior to the
// current street.
void EndgameSolver2::Walk(Node *node, unsigned int gbd, unsigned int solve_st,
			  const string &action_sequence, double **reach_probs,
			  unsigned int num_street_bets, bool resolving,
			  CFRValues *sumprobs, unsigned int root_bd_st,
			  unsigned int root_bd, HandTree *hand_tree,
			  const Buckets &buckets,
			  const CardAbstraction &card_abstraction,
			  unsigned int target_p, unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    StreetInitial(node, gbd, solve_st, action_sequence, reach_probs, sumprobs,
		  root_bd_st, root_bd, hand_tree, buckets, card_abstraction,
		  resolving, target_p);
    return;
  }
  bool new_resolving = resolving || (st == solve_st);
  if (new_resolving && ! resolving) {
    InitialResolve(node, gbd, solve_st, action_sequence, reach_probs,
		   num_street_bets, target_p);
    return;
  }
  // Always do progressive resolve at next street-initial node.
  // If progressive is true, also do it at any target player choice node
  bool resolve = (new_resolving &&
		  ((num_street_bets == 0 && node->PlayerActing() == 0) ||
		   internal_));
  // (internal_ && target_p == node->PlayerActing())));

  BettingTree *subtree = nullptr;
  if (resolve) {
    subtree = CreateSubtree(node, false, num_street_bets, target_p);
  }

  // If we're doing a resolve here, then we have created a new subtree
  // here, and when we recurse, we want to recurse within the new subtree.
  Node *new_node = nullptr;
  HandTree *new_hand_tree = nullptr;
  if (resolve) {
    fprintf(stderr, "Target P %u old nt %u gbd: %u action: %s\n", target_p,
	    node->NonterminalID(), gbd, action_sequence.c_str());
    for (unsigned int cfr_target_p = 0; cfr_target_p <= 1; ++cfr_target_p) {
      // Note that we pass in node rather than subtree->Root().  We need to
      // use the betting tree that corresponds to sumprobs.
      DynamicCBR dynamic_cbr;
      double *t_vals = dynamic_cbr.Compute(node, reach_probs, gbd, hand_tree,
					   sumprobs, root_bd_st, root_bd,
					   buckets, card_abstraction,
					   cfr_target_p^1, false, true);
      if (st > root_bd_st) {
	new_hand_tree = new HandTree(st, gbd, Game::MaxStreet());
      } else {
	new_hand_tree = hand_tree;
      }
      
      // Pass in false for both players.  I am doing separate solves for
      // each player.
      EGCFR eg_cfr(endgame_card_abstraction_, base_card_abstraction_,
		   endgame_betting_abstraction_, base_betting_abstraction_,
		   endgame_cfr_config_, base_cfr_config_, endgame_buckets_,
		   st, method_, cfrs_, zero_sum_, 1);
      eg_cfr.SolveSubgame(subtree, gbd, reach_probs, new_hand_tree, t_vals,
			  cfr_target_p, false, num_endgame_its_);
      delete [] t_vals;
      WriteEndgame(subtree->Root(), action_sequence, action_sequence, gbd,
		   base_card_abstraction_, endgame_card_abstraction_,
		   base_betting_abstraction_, endgame_betting_abstraction_,
		   base_cfr_config_, endgame_cfr_config_, method_,
		   eg_cfr.Sumprobs(), st, gbd, target_p, cfr_target_p, st);
    }
    new_node = subtree->Root();
    // If we return from Walk() after the first resolve, then we will never
    // get to an embedded node at which we could do a nested resolve.
    if (! nested_) return;
  } else {
    new_node = node;
    new_hand_tree = hand_tree;
  }
  
  CFRValues *new_sumprobs = nullptr;
  unsigned int new_root_bd_st = kMaxUInt, new_root_bd = kMaxUInt;
  const Buckets &new_buckets = new_resolving ? endgame_buckets_ : buckets;
  const CardAbstraction &new_card_abstraction =
    new_resolving ? endgame_card_abstraction_ : card_abstraction;
  
  if (resolve) {
    // This is a little wasteful.  We wrote the sumprobs out inside
    // Resolve().  Now we read them back in.  new_sumprobs now has the resolved
    // strategy for the subgame.  Nonterminal indices are those from subtree.
    new_sumprobs = ReadEndgame(action_sequence, subtree, gbd,
			       base_card_abstraction_,
			       endgame_card_abstraction_,
			       base_betting_abstraction_,
			       endgame_betting_abstraction_,
			       base_cfr_config_, endgame_cfr_config_,
			       endgame_buckets_, method_, st, gbd,  target_p);
    new_root_bd_st = st;
    new_root_bd = gbd;
  } else {
    new_sumprobs = sumprobs;
    new_root_bd_st = root_bd_st;
    new_root_bd = root_bd;
  }

  double ***succ_reach_probs =
    GetSuccReachProbs(new_node, gbd, new_hand_tree, new_buckets, new_sumprobs,
		      reach_probs, new_resolving);
  
  unsigned int num_succs = new_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = new_node->ActionName(s);
    unsigned int new_num_street_bets;
    if (s == new_node->CallSuccIndex() || s == new_node->FoldSuccIndex()) {
      new_num_street_bets = num_street_bets;
    } else {
      new_num_street_bets = num_street_bets + 1;
    }
    Walk(new_node->IthSucc(s), gbd, solve_st, action_sequence + action,
	 succ_reach_probs[s], new_num_street_bets, new_resolving,
	 new_sumprobs, new_root_bd_st, new_root_bd, new_hand_tree, new_buckets,
	 new_card_abstraction, target_p, st);
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    for (unsigned int p = 0; p < 2; ++p) {
      delete [] succ_reach_probs[s][p];
    }
    delete [] succ_reach_probs[s];
  }
  delete [] succ_reach_probs;
  if (resolve) {
    delete new_sumprobs;
  }
  if (subtree) {
    delete subtree;
  }
  if (new_hand_tree != hand_tree) {
    delete new_hand_tree;
  }
}

void EndgameSolver2::Walk(unsigned int solve_st, unsigned int target_p) {
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
  Walk(base_betting_tree_->Root(), 0, solve_st, "", reach_probs, 0, false,
       trunk_sumprobs_.get(), 0, 0, &preflop_hand_tree, base_buckets_,
       base_card_abstraction_, target_p, 0);
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] reach_probs[p];
  }
  delete [] reach_probs;
}

EndgameSolver2::EndgameSolver2(const CardAbstraction &base_ca,
			       const CardAbstraction &endgame_ca,
			       const BettingAbstraction &base_ba,
			       const BettingAbstraction &endgame_ba,
			       const CFRConfig &base_cc,
			       const CFRConfig &endgame_cc,
			       const Buckets &base_buckets,
			       const Buckets &endgame_buckets,
			       unsigned int base_it, unsigned int solve_st,
			       ResolvingMethod method, bool cfrs,
			       bool zero_sum, unsigned int num_its,
			       bool nested, bool internal, bool progressive) :
  base_card_abstraction_(base_ca), endgame_card_abstraction_(endgame_ca),
  base_betting_abstraction_(base_ba), endgame_betting_abstraction_(endgame_ba),
  base_cfr_config_(base_cc), endgame_cfr_config_(endgame_cc),
  base_buckets_(base_buckets), endgame_buckets_(endgame_buckets) {
  base_it_ = base_it;
  method_ = method;
  cfrs_ = cfrs;
  zero_sum_ = zero_sum;
  num_endgame_its_ = num_its;
  nested_ = nested;
  internal_ = internal;
  progressive_ = progressive;

  unsigned int max_street = Game::MaxStreet();
  base_betting_tree_ = BettingTree::BuildTree(base_betting_abstraction_);
  // We need probs for both players
  unique_ptr<bool []> trunk_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    trunk_streets[st] = st < solve_st;
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
				      base_betting_tree_, 0, 0,
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

  street_buckets_ = new unsigned int *[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    street_buckets_[st] = new unsigned int[num_hole_card_pairs];
  }
  
#if 0
  // Don't think I support bucketed system yet.  Will want to set current
  // strategy only for the subtree that I load for resolving.
  unique_ptr<bool []> bucketed_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucketed_streets[st] = ! buckets_.None(st);
    if (bucketed_streets[st]) bucketed_ = true;
  }
  if (bucketed_) {
    // Current strategy always uses doubles
    // Want opponent's strategy
    // This doesn't generalize to multiplayer
    current_strategy_.reset(new CFRValues(nullptr, false,
					  bucketed_streets.get(),
					  base_betting_tree_, 0, 0,
					  base_card_abstraction_, base_buckets_,
					  compressed_streets_));
    current_strategy_->AllocateAndClearDoubles(base_betting_tree_->Root(),
					       kMaxUInt);
  } else {
    current_strategy_.reset(nullptr);
  }

  if (current_strategy_.get() != nullptr) {
    SetCurrentStrategy(base_betting_tree_->Root());
  }
#endif
}
 
EndgameSolver2::~EndgameSolver2(void) {
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int st = 0; st <= max_street; ++st) {
    delete [] street_buckets_[st];
  }
  delete [] street_buckets_;
  delete base_betting_tree_;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card params> "
	  "<endgame card params> <base betting params> "
	  "<endgame betting params> <base CFR params> <endgame CFR params> "
	  "<solve street> <target street> <base it> <num endgame its> "
	  "[unsafe|cfrd|maxmargin|combined] [cbrs|cfrs] [zerosum|raw] "
	  "[nested|notnested] [internal|notinternal] "
	  "[progressive|notprogressive]\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 18) Usage(argv[0]);
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
  unsigned int solve_st, target_st, base_it, num_endgame_its;
  if (sscanf(argv[8], "%u", &solve_st) != 1)         Usage(argv[0]);
  if (sscanf(argv[9], "%u", &target_st) != 1)        Usage(argv[0]);
  if (sscanf(argv[10], "%u", &base_it) != 1)         Usage(argv[0]);
  if (sscanf(argv[11], "%u", &num_endgame_its) != 1) Usage(argv[0]);
  string m = argv[12];
  ResolvingMethod method;
  if (m == "unsafe")         method = ResolvingMethod::UNSAFE;
  else if (m == "cfrd")      method = ResolvingMethod::CFRD;
  else if (m == "maxmargin") method = ResolvingMethod::MAXMARGIN;
  else if (m == "combined")  method = ResolvingMethod::COMBINED;
  else                       Usage(argv[0]);
  string v = argv[13];
  bool cfrs;
  if (v == "cbrs")      cfrs = false;
  else if (v == "cfrs") cfrs = true;
  else                  Usage(argv[0]);
  string z = argv[14];
  bool zero_sum;
  if (z == "zerosum")  zero_sum = true;
  else if (z == "raw") zero_sum = false;
  else                 Usage(argv[0]);
  string n = argv[15];
  bool nested;
  if (n == "nested")         nested = true;
  else if (n == "notnested") nested = false;
  else                       Usage(argv[0]);
  string i = argv[16];
  bool internal;
  if (i == "internal")         internal = true;
  else if (i == "notinternal") internal = false;
  else                          Usage(argv[0]);
  string p = argv[17];
  bool progressive;
  if (p == "progressive")         progressive = true;
  else if (p == "notprogressive") progressive = false;
  else                            Usage(argv[0]);

  Buckets base_buckets(*base_card_abstraction, false);
  Buckets endgame_buckets(*endgame_card_abstraction, false);

  BoardTree::Create();

  EndgameSolver2 solver(*base_card_abstraction, *endgame_card_abstraction,
			*base_betting_abstraction,
			*endgame_betting_abstraction, *base_cfr_config,
			*endgame_cfr_config, base_buckets, endgame_buckets,
			base_it, solve_st, method, cfrs, zero_sum,
			num_endgame_its, nested, internal, progressive);
  for (unsigned int target_p = 0; target_p <= 1; ++target_p) {
    DeleteAllEndgames(*base_card_abstraction, *endgame_card_abstraction,
		      *base_betting_abstraction, *endgame_betting_abstraction,
		      *base_cfr_config, *endgame_cfr_config, method, target_p);
    solver.Walk(solve_st, target_p);
  }
}
