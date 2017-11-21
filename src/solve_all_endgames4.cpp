// Like solve_all_endgames, but we will use DynamicCBR2 to compute the
// opponent counterfactual values.
//
// We only solve subgames at street-initial nodes.  There is no nested
// subgame solving.
//
// Eventually this should become a good imitation of what we would do in
// the bot.  That means we should not assume we can load the trunk sumprobs
// in memory.  Can I assume a hand tree for the trunk in memory?
//
// For symmetric systems, should I be create four endgame directories, or
// just two?  I think just two.
//
// solve_all_endgames4 is much slower than solve_all_endgames because we
// repeatedly read the entire base strategy in (inside
// ReadBaseEndgameStrategy()).
//
// How do I want to multithread?  Single EndgameSolver4 object?  Probably
// need that right now because sumprobs and regrets are unique_ptr members.
// dynamic_cbr_ will then be shared by all threads.  Doesn't work with
// disk-based system.  Each thread reads different sumprobs object, sets
// DynamicCBR2 object differently.

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
#include "dynamic_cbr2.h"
#include "eg_cfr.h"
#include "endgame_utils.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"
#include "split.h"
#include "vcfr.h"

class EndgameSolver4 {
public:
  EndgameSolver4(const CardAbstraction &base_card_abstraction,
		 const CardAbstraction &endgame_card_abstraction,
		 const BettingAbstraction &base_betting_abstraction,
		 const BettingAbstraction &endgame_betting_abstraction,
		 const CFRConfig &base_cfr_config,
		 const CFRConfig &endgame_cfr_config,
		 const Buckets &base_buckets, const Buckets &endgame_buckets,
		 unsigned int solve_st, ResolvingMethod method,
		 bool cfrs, bool card_level, bool zero_sum, bool current,
		 const bool *pure_streets, bool base_mem, unsigned int base_it,
		 unsigned int num_endgame_its, unsigned int num_threads);
  ~EndgameSolver4(void) {}
  void Walk(Node *node, const string &action_sequence, unsigned int gbd,
	    double **reach_probs, unsigned int last_st);
  void Walk(void);
private:
  BettingTree *CreateSubtree(Node *node, unsigned int target_p, bool base);
  void Split(Node *node, const string &action_sequence,
	     unsigned int pgbd, double **reach_probs);
  void StreetInitial(Node *node, const string &action_sequence,
		     unsigned int pgbd, double **reach_probs);
  void Resolve(Node *node, unsigned int gbd,
	       const string &action_sequence, double **reach_probs);

  const CardAbstraction &base_card_abstraction_;
  const CardAbstraction &endgame_card_abstraction_;
  const BettingAbstraction &base_betting_abstraction_;
  const BettingAbstraction &endgame_betting_abstraction_;
  const CFRConfig &base_cfr_config_;
  const CFRConfig &endgame_cfr_config_;
  const Buckets &base_buckets_;
  const Buckets &endgame_buckets_;
  unique_ptr<BettingTree> base_betting_tree_;
  unique_ptr<HandTree> trunk_hand_tree_;
  unique_ptr<CFRValues> trunk_sumprobs_;
  unique_ptr<DynamicCBR2> dynamic_cbr_;
  unsigned int solve_st_;
  ResolvingMethod method_;
  bool cfrs_;
  bool card_level_;
  bool zero_sum_;
  bool current_;
  const bool *pure_streets_;
  bool base_mem_;
  unsigned int base_it_;
  unsigned int num_endgame_its_;
  unsigned int num_threads_;
};

EndgameSolver4::EndgameSolver4(const CardAbstraction &base_card_abstraction,
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
			       bool cfrs, bool card_level, bool zero_sum,
			       bool current, const bool *pure_streets,
			       bool base_mem, unsigned int base_it,
			       unsigned int num_endgame_its,
			       unsigned int num_threads) :
  base_card_abstraction_(base_card_abstraction),
  endgame_card_abstraction_(endgame_card_abstraction),
  base_betting_abstraction_(base_betting_abstraction),
  endgame_betting_abstraction_(endgame_betting_abstraction),
  base_cfr_config_(base_cfr_config), endgame_cfr_config_(endgame_cfr_config),
  base_buckets_(base_buckets), endgame_buckets_(endgame_buckets) {
  solve_st_ = solve_st;
  method_ = method;
  cfrs_ = cfrs;
  card_level_ = card_level;
  zero_sum_ = zero_sum;
  current_ = current;
  pure_streets_ = pure_streets;
  base_mem_ = base_mem;
  base_it_ = base_it;
  num_endgame_its_ = num_endgame_its;
  num_threads_ = num_threads;
  
  base_betting_tree_.reset(BettingTree::BuildTree(base_betting_abstraction_));

  // We need probs for both players
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> trunk_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    trunk_streets[st] = (base_mem_ && ! current_) || st < solve_st_;
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
  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
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
  trunk_sumprobs_->Read(dir, base_it_, base_betting_tree_->Root(), "x",
			kMaxUInt);

  if (base_mem_) {
    // We are calculating CBRs from the *base* strategy, not the resolved
    // endgame strategy.  So pass in base_card_abstraction_, etc.
    dynamic_cbr_.reset(new DynamicCBR2(base_card_abstraction_,
				       base_betting_abstraction_,
				       base_cfr_config_, base_buckets_, 1));
    if (current_) {
      unique_ptr<bool []> endgame_streets(new bool[max_street + 1]);
      for (unsigned int st = 0; st <= max_street; ++st) {
	endgame_streets[st] = st >= solve_st_;
      }
      unique_ptr<CFRValues> regrets;
      regrets.reset(new CFRValues(nullptr, false, endgame_streets.get(),
				  base_betting_tree_.get(), 0, 0,
				  base_card_abstraction_, base_buckets_,
				  compressed_streets.get()));
      regrets->Read(dir, base_it_, base_betting_tree_->Root(), "x",
		    kMaxUInt);
      dynamic_cbr_->MoveRegrets(regrets);
    } else {
      dynamic_cbr_->MoveSumprobs(trunk_sumprobs_);
    }
  }

  if (base_mem_) {
    trunk_hand_tree_.reset(new HandTree(0, 0, max_street));
  } else {
    if (solve_st_ > 0) {
      trunk_hand_tree_.reset(new HandTree(0, 0, solve_st_ - 1));
    } else {
      trunk_hand_tree_.reset(new HandTree(0, 0, 0));
    }
  }
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
BettingTree *EndgameSolver4::CreateSubtree(Node *node, unsigned int target_p,
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
  // Delete the nodes under subtree_root?  Or does garbage collection
  // automatically take care of it because they are shared pointers.
  return BettingTree::BuildSubtree(subtree_root.get());
}

class ES4Thread {
public:
  ES4Thread(Node *node, const string &action_sequence, unsigned int pgbd,
	    double **reach_probs, EndgameSolver4 *solver,
	    unsigned int thread_index, unsigned int num_threads);
  ~ES4Thread(void) {}
  void Run(void);
  void Join(void);
  void Go(void);
private:
  Node *node_;
  string action_sequence_;
  unsigned int pgbd_;
  double **reach_probs_;
  EndgameSolver4 *solver_;
  unsigned int thread_index_;
  unsigned int num_threads_;
  pthread_t pthread_id_;
};

ES4Thread::ES4Thread(Node *node, const string &action_sequence,
		     unsigned int pgbd, double **reach_probs,
		     EndgameSolver4 *solver, unsigned int thread_index,
		     unsigned int num_threads) :
  action_sequence_(action_sequence) {
  node_ = node;
  pgbd_ = pgbd;
  reach_probs_ = reach_probs;
  solver_ = solver;
  thread_index_ = thread_index;
  num_threads_ = num_threads;
}

void ES4Thread::Go(void) {
  unsigned int nst = node_->Street();
  unsigned int pst = nst - 1;
  unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd_, nst);
  unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd_, nst);
  for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
    if (ngbd % num_threads_ != thread_index_) continue;
    solver_->Walk(node_, action_sequence_, ngbd, reach_probs_, nst);
  }
}

static void *es4_thread_run(void *v_t) {
  ES4Thread *t = (ES4Thread *)v_t;
  t->Go();
  return NULL;
}

void ES4Thread::Run(void) {
  pthread_create(&pthread_id_, NULL, es4_thread_run, this);
}

void ES4Thread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

void EndgameSolver4::Split(Node *node, const string &action_sequence,
			   unsigned int pgbd, double **reach_probs) {
  unique_ptr<ES4Thread * []> threads(new ES4Thread *[num_threads_]);
  for (unsigned int t = 0; t < num_threads_; ++t) {
    threads[t] = new ES4Thread(node, action_sequence, pgbd, reach_probs, this,
			       t, num_threads_);
  }
  for (unsigned int t = 1; t < num_threads_; ++t) {
    threads[t]->Run();
  }
  // Do first thread in main thread
  threads[0]->Go();
  for (unsigned int t = 1; t < num_threads_; ++t) {
    threads[t]->Join();
  }
  for (unsigned int t = 0; t < num_threads_; ++t) {
    delete threads[t];
  }
}

void EndgameSolver4::StreetInitial(Node *node, const string &action_sequence,
				   unsigned int pgbd, double **reach_probs) {
  unsigned int nst = node->Street();
  if (nst == 1 && num_threads_ > 1) {
    Split(node, action_sequence, pgbd, reach_probs);
  } else {
    unsigned int pst = node->Street() - 1;
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(pst, pgbd, nst);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(pst, pgbd, nst);
    for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      Walk(node, action_sequence, ngbd, reach_probs, nst);
    }
  }
}

// Currently assume that this is a street-initial node.
// Might need to do up to four solves.  Imagine we have an asymmetric base
// betting tree, and an asymmetric solving method.
void EndgameSolver4::Resolve(Node *node, unsigned int gbd,
			     const string &action_sequence,
			     double **reach_probs) {
  unsigned int st = node->Street();
  printf("Resolve %s st %u nt %u gbd %u\n",
	 action_sequence.c_str(), st, node->NonterminalID(), gbd);
  unsigned int num_players = Game::NumPlayers();
  HandTree hand_tree(st, gbd, Game::MaxStreet());
  bool cfrs = false;
  bool zero_sum = true;
  EGCFR eg_cfr(endgame_card_abstraction_, base_card_abstraction_,
	       endgame_betting_abstraction_, base_betting_abstraction_,
	       endgame_cfr_config_, base_cfr_config_, endgame_buckets_,
	       st, method_, cfrs, zero_sum, 1);
  
  unsigned int num_asym_players =
    base_betting_abstraction_.Asymmetric() ? num_players : 1;
  for (unsigned int asym_p = 0; asym_p < num_asym_players; ++asym_p) {
    BettingTree *base_subtree = nullptr;
    BettingTree *endgame_subtree = CreateSubtree(node, asym_p, false);
    if (! base_mem_) {
      base_subtree = CreateSubtree(node, asym_p, true);
      // The action sequence passed in should specify the root of the system
      // we are reading (the base system).
      unique_ptr<CFRValues> base_subgame_strategy(
	    ReadBaseEndgameStrategy(base_card_abstraction_,
				    base_betting_abstraction_,
				    base_cfr_config_, base_betting_tree_.get(),
				    base_buckets_, endgame_buckets_, base_it_,
				    node,  gbd, "x", reach_probs, base_subtree,
				    current_, asym_p));
      // We are calculating CBRs from the *base* strategy, not the resolved
      // endgame strategy.  So pass in base_card_abstraction_, etc.
      dynamic_cbr_.reset(new DynamicCBR2(base_card_abstraction_,
					 base_betting_abstraction_,
					 base_cfr_config_, base_buckets_, 1));
      if (current_) dynamic_cbr_->MoveRegrets(base_subgame_strategy);
      else          dynamic_cbr_->MoveSumprobs(base_subgame_strategy);
    }

    if (method_ == ResolvingMethod::UNSAFE) {
      eg_cfr.SolveSubgame(endgame_subtree, gbd, reach_probs, action_sequence,
			  &hand_tree, nullptr, kMaxUInt, true,
			  num_endgame_its_);
      // Write out the P0 and P1 strategies
      for (unsigned int solve_p = 0; solve_p < num_players; ++solve_p) {
	WriteEndgame(endgame_subtree->Root(), action_sequence, action_sequence,
		     gbd, base_card_abstraction_, endgame_card_abstraction_,
		     base_betting_abstraction_, endgame_betting_abstraction_,
		     base_cfr_config_, endgame_cfr_config_, method_,
		     eg_cfr.Sumprobs(), st, gbd, asym_p, solve_p, st);
      }
    } else {
      for (unsigned int solve_p = 0; solve_p < num_players; ++solve_p) {
	// What should I be supplying for the betting abstraction, CFR
	// config and betting tree?  Does it matter?
	if (! card_level_) {
	  fprintf(stderr, "DynamicCBR cannot compute bucket-level CVs\n");
	  exit(-1);
	}
	unique_ptr<double []> t_vals;
	if (base_mem_) {
	  // When base_mem_ is true, we use a global betting tree, a global
	  // hand tree and have a global base strategy.
	  // We assume that pure_streets_[st] tells us whether to purify
	  // for the entire endgame.
	  t_vals.reset(dynamic_cbr_->Compute(node, reach_probs, gbd,
					     trunk_hand_tree_.get(), 0, 0,
					     solve_p^1, cfrs_, zero_sum_,
					     current_, pure_streets_[st]));
	} else {
	  // base_subtree is just for the subgame
	  // st and gbd define the subgame
	  t_vals.reset(dynamic_cbr_->Compute(base_subtree->Root(),
					     reach_probs, gbd, &hand_tree,
					     st, gbd, solve_p^1, cfrs_,
					     zero_sum_, current_,
					     pure_streets_[st]));
	}
	
	// Pass in false for both_players.  I am doing separate solves for
	// each player.
	eg_cfr.SolveSubgame(endgame_subtree, gbd, reach_probs, action_sequence,
			    &hand_tree, t_vals.get(), solve_p, false,
			    num_endgame_its_);
	      
	WriteEndgame(endgame_subtree->Root(), action_sequence, action_sequence,
		     gbd, base_card_abstraction_, endgame_card_abstraction_,
		     base_betting_abstraction_, endgame_betting_abstraction_,
		     base_cfr_config_, endgame_cfr_config_, method_,
		     eg_cfr.Sumprobs(), st, gbd, asym_p, solve_p, st);
      }
    }
  
    delete base_subtree;
    delete endgame_subtree;
  }
}

void EndgameSolver4::Walk(Node *node, const string &action_sequence,
			  unsigned int gbd, double **reach_probs,
			  unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    StreetInitial(node, action_sequence, gbd, reach_probs);
    return;
  }
  if (st == solve_st_) {
    // Do we assume that this is a street-initial node?
    // We do assume no bet pending
    Resolve(node, gbd, action_sequence, reach_probs);
    return;
  }
  
  Node *new_node = node;
  
  const CFRValues *sumprobs;
  if (base_mem_ && ! current_) {
    sumprobs = dynamic_cbr_->Sumprobs();
  } else {
    sumprobs = trunk_sumprobs_.get();
  }
  double ***succ_reach_probs =
    GetSuccReachProbs(new_node, gbd, trunk_hand_tree_.get(), base_buckets_,
		      sumprobs, reach_probs, 0, 0, pure_streets_[st]);
  
  unsigned int num_succs = new_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = new_node->ActionName(s);
    Walk(new_node->IthSucc(s), action_sequence + action, gbd,
	 succ_reach_probs[s], st);
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    for (unsigned int p = 0; p < 2; ++p) {
      delete [] succ_reach_probs[s][p];
    }
    delete [] succ_reach_probs[s];
  }
  delete [] succ_reach_probs;
}

void EndgameSolver4::Walk(void) {
  unsigned int num_players = Game::NumPlayers();
  double **reach_probs = new double *[num_players];
  unsigned int max_card1 = Game::MaxCard() + 1;
  unsigned int num_enc = max_card1 * max_card1;
  const CanonicalCards *preflop_hands = trunk_hand_tree_->Hands(0, 0);
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
  Walk(base_betting_tree_->Root(), "x", 0, reach_probs, 0);
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
	  "[unsafe|cfrd|maxmargin|combined] [cbrs|cfrs] [card|bucket] "
	  "[zerosum|raw] [current|avg] <pure streets> [mem|disk] "
	  "<num threads>\n", prog_name);
  fprintf(stderr, "\n");
  fprintf(stderr, "\"current\" or \"avg\" signifies whether we use the "
	  "opponent's current strategy (from regrets) in the endgame CBR "
	  " calculation, or, as per usual, the avg strategy (from "
	  "sumprobs)\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "pure streets are those streets on which to purify "
	  "probabilities.  In the endgame, this means purifying the "
	  "opponent's strategy when computing the endgame CBRs.  In the "
	  "trunk, this means purifying the reach probs for *both* players.  "
	  "Use \"null\" to signify no pure streets.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "\"mem\" or \"disk\" signifies whether the base strategy "
	  "for the endgame streets is loaded into memory at startup, or "
	  "whether we read the base endgame strategy as needed.  Note that "
	  "the trunk streets are loaded into memory at startup "
	  "regardless.\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 19) Usage(argv[0]);
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
  string v = argv[12];
  bool cfrs;
  if (v == "cbrs")      cfrs = false;
  else if (v == "cfrs") cfrs = true;
  else                  Usage(argv[0]);
  string l = argv[13];
  bool card_level;
  if (l == "card")        card_level = true;
  else if (l == "bucket") card_level = false;
  else                    Usage(argv[0]);
  string z = argv[14];
  bool zero_sum;
  if (z == "zerosum")  zero_sum = true;
  else if (z == "raw") zero_sum = false;
  else                 Usage(argv[0]);
  string c = argv[15];
  bool current;
  if (c == "current")  current = true;
  else if (c == "avg") current = false;
  else                 Usage(argv[0]);
  unsigned int max_street = Game::MaxStreet();
  unique_ptr<bool []> pure_streets(new bool[max_street + 1]);
  string p = argv[16];
  for (unsigned int st = 0; st <= max_street; ++st) pure_streets[st] = false;
  if (p != "null") {
    vector<unsigned int> v;
    ParseUnsignedInts(p, &v);
    unsigned int num = v.size();
    for (unsigned int i = 0; i < num; ++i) {
      pure_streets[v[i]] = true;
    }
  }
  string mem = argv[17];
  bool base_mem;
  if (mem == "mem")       base_mem = true;
  else if (mem == "disk") base_mem = false;
  else                    Usage(argv[0]);
  unsigned int num_threads;
  if (sscanf(argv[18], "%u", &num_threads) != 1) Usage(argv[0]);

  // If card abstractions are the same, should not load both.
  Buckets base_buckets(*base_card_abstraction, false);
  Buckets endgame_buckets(*endgame_card_abstraction, false);

  BoardTree::Create();

  EndgameSolver4 solver(*base_card_abstraction, *endgame_card_abstraction,
			*base_betting_abstraction,
			*endgame_betting_abstraction, *base_cfr_config,
			*endgame_cfr_config, base_buckets, endgame_buckets,
			solve_st, method, cfrs, card_level, zero_sum, current,
			pure_streets.get(), base_mem, base_it,
			num_endgame_its, num_threads);
  for (unsigned int asym_p = 0; asym_p <= 1; ++asym_p) {
    DeleteAllEndgames(*base_card_abstraction, *endgame_card_abstraction,
		      *base_betting_abstraction, *endgame_betting_abstraction,
		      *base_cfr_config, *endgame_cfr_config, method, asym_p);
  }
  solver.Walk();
}
