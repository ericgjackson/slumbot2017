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
#include "eg_cfr.h"
#include "endgame_utils.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "nl_agent.h"
#include "params.h"
#include "runtime_config.h"
#include "runtime_params.h"
#include "split.h"
#include "vcfr.h"

class EndgameSolver5 {
public:
  EndgameSolver5(const CardAbstraction &base_card_abstraction,
		 const CardAbstraction &endgame_card_abstraction,
		 const BettingAbstraction &base_betting_abstraction,
		 const BettingAbstraction &endgame_betting_abstraction,
		 const CFRConfig &base_cfr_config,
		 const CFRConfig &endgame_cfr_config,
		 const Buckets &base_buckets, const Buckets &endgame_buckets,
		 unsigned int solve_st, ResolvingMethod method,
		 bool cfrs, bool card_level, bool zero_sum, bool current,
		 const bool *pure_streets, bool base_mem, unsigned int base_it,
		 unsigned int num_endgame_its, unsigned int num_threads,
		 unsigned int asym_p);
  ~EndgameSolver5(void);
  void Walk(Node *node, const string &action_sequence, unsigned int gbd,
	    double **reach_probs, unsigned int last_st);
  void Walk(void);
private:
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
  unsigned int asym_p_;
  NLAgent *agent_;
};

EndgameSolver5::EndgameSolver5(const CardAbstraction &base_card_abstraction,
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
			       unsigned int num_threads,
			       unsigned int asym_p) :
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
  asym_p_ = asym_p;

  if (base_betting_abstraction_.Asymmetric()) {
    base_betting_tree_.reset(
	BettingTree::BuildAsymmetricTree(base_betting_abstraction_, asym_p_));
  } else {
    base_betting_tree_.reset(
        BettingTree::BuildTree(base_betting_abstraction_));
  }

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
				      base_card_abstraction_,
				      base_buckets_.NumBuckets(),
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
    char buf[100];
    sprintf(buf, ".p%u", asym_p_);
    strcat(dir, buf);
  }
  trunk_sumprobs_->Read(dir, base_it_, base_betting_tree_->Root(), "x",
			kMaxUInt);

  if (base_mem_) {
    trunk_hand_tree_.reset(new HandTree(0, 0, max_street));
  } else {
    if (solve_st_ > 0) {
      trunk_hand_tree_.reset(new HandTree(0, 0, solve_st_ - 1));
    } else {
      trunk_hand_tree_.reset(new HandTree(0, 0, 0));
    }
  }

  unique_ptr<Params> runtime_params = CreateRuntimeParams();
  runtime_params->ReadFromFile("/home/eric/slumbot2017/runs/runtime_params");
  RuntimeConfig *runtime_config = new RuntimeConfig(*runtime_params);
  unsigned int num_players = Game::NumPlayers();
  unsigned int *iterations = new unsigned int[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    iterations[p] = base_it_;
  }
  
  BettingTree **betting_trees = new BettingTree *[num_players];
  if (base_betting_abstraction_.Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      betting_trees[p] =
	BettingTree::BuildAsymmetricTree(base_betting_abstraction_, p);
    }
  } else {
    BettingTree *betting_tree =
      BettingTree::BuildTree(base_betting_abstraction_);
    for (unsigned int p = 0; p < num_players; ++p) {
      betting_trees[p] = betting_tree;
    }
  }

  bool debug = false, exit_on_error = true;
  unsigned int small_blind = 50;
  unsigned int stack_size = 20000;
  agent_ = new NLAgent(base_card_abstraction_, endgame_card_abstraction_,
		       base_betting_abstraction_, endgame_betting_abstraction_,
		       base_cfr_config_, endgame_cfr_config_, *runtime_config,
		       iterations, betting_trees, solve_st_, num_endgame_its_,
		       debug, exit_on_error, small_blind, stack_size);
  
}

EndgameSolver5::~EndgameSolver5(void) {
  delete agent_;
}

class ES4Thread {
public:
  ES4Thread(Node *node, const string &action_sequence, unsigned int pgbd,
	    double **reach_probs, EndgameSolver5 *solver,
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
  EndgameSolver5 *solver_;
  unsigned int thread_index_;
  unsigned int num_threads_;
  pthread_t pthread_id_;
};

ES4Thread::ES4Thread(Node *node, const string &action_sequence,
		     unsigned int pgbd, double **reach_probs,
		     EndgameSolver5 *solver, unsigned int thread_index,
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

void EndgameSolver5::Split(Node *node, const string &action_sequence,
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

void EndgameSolver5::StreetInitial(Node *node, const string &action_sequence,
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

void EndgameSolver5::Walk(Node *node, const string &action_sequence,
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
    agent_->ResolveAndWrite(node, gbd, action_sequence, reach_probs);
    return;
  }
  
  Node *new_node = node;
  
  double ***succ_reach_probs =
    GetSuccReachProbs(new_node, gbd, trunk_hand_tree_.get(), base_buckets_,
		      trunk_sumprobs_.get(), reach_probs, 0, 0,
		      pure_streets_[st]);
  
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

void EndgameSolver5::Walk(void) {
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
	  "<num threads> (player)\n", prog_name);
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
  if (argc != 19 && argc != 20) Usage(argv[0]);
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
  unsigned int asym_p = kMaxUInt;
  if (base_betting_abstraction->Asymmetric()) {
    if (argc != 20) {
      Usage(argv[0]);
    }
    if (sscanf(argv[19]+1, "%u", &asym_p) != 1) Usage(argv[0]);
  } else {
    if (argc == 20) {
      Usage(argv[0]);
    }
  }

  // If card abstractions are the same, should not load both.
  Buckets base_buckets(*base_card_abstraction, false);
  Buckets endgame_buckets(*endgame_card_abstraction, false);

  BoardTree::Create();

  EndgameSolver5 solver(*base_card_abstraction, *endgame_card_abstraction,
			*base_betting_abstraction,
			*endgame_betting_abstraction, *base_cfr_config,
			*endgame_cfr_config, base_buckets, endgame_buckets,
			solve_st, method, cfrs, card_level, zero_sum, current,
			pure_streets.get(), base_mem, base_it,
			num_endgame_its, num_threads, asym_p);
  if (base_betting_abstraction->Asymmetric()) {
    DeleteAllEndgames(*base_card_abstraction, *endgame_card_abstraction,
		      *base_betting_abstraction, *endgame_betting_abstraction,
		      *base_cfr_config, *endgame_cfr_config, method, asym_p);
  } else {
    for (unsigned int p = 0; p <= 1; ++p) {
      DeleteAllEndgames(*base_card_abstraction, *endgame_card_abstraction,
			*base_betting_abstraction, *endgame_betting_abstraction,
			*base_cfr_config, *endgame_cfr_config, method, p);
    }
  }
  solver.Walk();
}
