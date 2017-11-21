// This version allows us to use a mixture of the base system and the
// resolved system on the solve streets.  The strategy prior to the solve
// street comes from the base.  The strategy after the solve street comes
// from the resolved system.
//
// Want to have one file per street.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_abstraction_params.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "canonical_cards.h"
#include "card_abstraction.h"
#include "card_abstraction_params.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "eg_cfr.h" // ResolvingMethod
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"
#include "split.h"

class Assembler {
public:
  Assembler(BettingTree *base_betting_tree, BettingTree *endgame_betting_tree,
	    unsigned int solve_street, unsigned int target_street,
	    unsigned int target_street_threshold,
	    unsigned int base_it, unsigned int endgame_it,
	    const CardAbstraction &base_ca, const CardAbstraction &endgame_ca,
	    const CardAbstraction &merged_ca,
	    const BettingAbstraction &base_ba,
	    const BettingAbstraction &endgame_ba, const CFRConfig &base_cc,
	    const CFRConfig &endgame_cc, const CFRConfig &merged_cc,
	    ResolvingMethod method);
  Assembler(void);
  ~Assembler(void);
  void Go(void);
private:
  void WalkTrunk(Node *base_node, Node *endgame_node, unsigned int last_st);

  bool asymmetric_;
  BettingTree *base_betting_tree_;
  BettingTree *endgame_betting_tree_;
  unsigned int solve_street_;
  unsigned int target_street_;
  unsigned int target_street_threshold_;
  unsigned int base_it_;
  unsigned int endgame_it_;
  const CardAbstraction &base_ca_;
  const CardAbstraction &endgame_ca_;
  const CardAbstraction &merged_ca_;
  const BettingAbstraction &base_ba_;
  const BettingAbstraction &endgame_ba_;
  const CFRConfig &base_cc_;
  const CFRConfig &endgame_cc_;
  const CFRConfig &merged_cc_;
  ResolvingMethod method_;
  unique_ptr<CFRValues> base_sumprobs_;
  unique_ptr<CFRValues> merged_sumprobs_;
  unique_ptr<Buckets> endgame_buckets_;
};

Assembler::Assembler(BettingTree *base_betting_tree,
		     BettingTree *endgame_betting_tree,
		     unsigned int solve_street, unsigned int target_street,
		     unsigned int target_street_threshold,
		     unsigned int base_it, unsigned int endgame_it,
		     const CardAbstraction &base_ca,
		     const CardAbstraction &endgame_ca,
		     const CardAbstraction &merged_ca,
		     const BettingAbstraction &base_ba,
		     const BettingAbstraction &endgame_ba,
		     const CFRConfig &base_cc,
		     const CFRConfig &endgame_cc,
		     const CFRConfig &merged_cc,
		     ResolvingMethod method) :
  base_ca_(base_ca), endgame_ca_(endgame_ca), merged_ca_(merged_ca),
  base_ba_(base_ba), endgame_ba_(endgame_ba), base_cc_(base_cc),
  endgame_cc_(endgame_cc), merged_cc_(merged_cc) {
  asymmetric_ = false;
  base_betting_tree_ = base_betting_tree;
  endgame_betting_tree_ = endgame_betting_tree;
  solve_street_ = solve_street;
  target_street_ = target_street;
  target_street_threshold_ = target_street_threshold;
  base_it_ = base_it;
  endgame_it_ = endgame_it;
  method_ = method;

  DeleteOldFiles(merged_ca_, endgame_ba_, merged_cc_, endgame_it_);

  endgame_buckets_.reset(new Buckets(endgame_ca_, true));
  
  unsigned int max_street = Game::MaxStreet();
  bool *base_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    // Include the target street
    base_streets[st] = st <= target_street;
  }
  Buckets base_buckets(base_ca_, true);

  bool *base_compressed_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    base_compressed_streets[st] = false;
  }
  const vector<unsigned int> &bcsv = base_cc_.CompressedStreets();
  unsigned int num_bcsv = bcsv.size();
  for (unsigned int i = 0; i < num_bcsv; ++i) {
    unsigned int st = bcsv[i];
    base_compressed_streets[st] = true;
  }

  CFRValues base_sumprobs(nullptr, true, base_streets, base_betting_tree_, 0,
			  0, base_ca_, base_buckets, base_compressed_streets);
  delete [] base_compressed_streets;
  delete [] base_streets;
  
  char read_dir[500], write_dir[500];
  sprintf(read_dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), base_ca.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  base_ba_.BettingAbstractionName().c_str(),
	  base_cc_.CFRConfigName().c_str());
  base_sumprobs.Read(read_dir, base_it, base_betting_tree_->Root(),
		     base_betting_tree->Root()->NonterminalID(), kMaxUInt);
  sprintf(write_dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(), merged_ca_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  endgame_ba_.BettingAbstractionName().c_str(),
	  merged_cc_.CFRConfigName().c_str());
  base_sumprobs.Write(write_dir, endgame_it_, base_betting_tree_->Root(),
		      base_betting_tree_->Root()->NonterminalID(),
		      kMaxUInt);

  bool *endgame_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    endgame_streets[st] = st >= target_street_;
  }
  Buckets merged_buckets(merged_ca_, true);

  bool *merged_compressed_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    merged_compressed_streets[st] = false;
  }
  const vector<unsigned int> &mcsv = merged_cc_.CompressedStreets();
  unsigned int num_mcsv = mcsv.size();
  for (unsigned int i = 0; i < num_mcsv; ++i) {
    unsigned int st = mcsv[i];
    merged_compressed_streets[st] = true;
  }

  merged_sumprobs_.reset(new CFRValues(nullptr, true, endgame_streets,
				       endgame_betting_tree_, 0, 0,
				       merged_ca_, merged_buckets,
				       merged_compressed_streets));
  delete [] merged_compressed_streets;
  delete [] endgame_streets;

  merged_sumprobs_->MergeInto(base_sumprobs, 0, base_betting_tree_->Root(),
			      endgame_betting_tree_->Root(), base_buckets,
			      target_street_);
}

Assembler::~Assembler(void) {
}

// When we get to the target street, read the entire base strategy for
// this subtree into merged sumprobs.  Then go through and override parts
// with the endgame strategy.
void Assembler::WalkTrunk(Node *base_node, Node *endgame_node,
			  unsigned int last_st) {
  if (base_node->Terminal()) return;
  unsigned int st = base_node->Street();
  if (st == target_street_) {
    if (base_node->PotSize() >= target_street_threshold_) {
      unsigned int num_boards = BoardTree::NumBoards(st);
      unsigned int base_subtree_nt = base_node->NonterminalID();
      fprintf(stderr, "Base subtree NT: %u\n", base_subtree_nt);
      BettingTree *subtree = BettingTree::BuildSubtree(endgame_node);
      unsigned int max_street = Game::MaxStreet();
      bool *endgame_streets = new bool[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	endgame_streets[st] = st >= target_street_;
      }
      bool *endgame_compressed_streets = new bool[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	endgame_compressed_streets[st] = false;
      }
      const vector<unsigned int> &ecsv = endgame_cc_.CompressedStreets();
      unsigned int num_ecsv = ecsv.size();
      for (unsigned int i = 0; i < num_ecsv; ++i) {
	unsigned int st = ecsv[i];
	endgame_compressed_streets[st] = true;
      }
      char read_dir[500];
      sprintf(read_dir, "%s/%s.%s.%u.%u.%u.%s.%s/endgames.%s.%s.%s.%s.%u.%u",
	      Files::OldCFRBase(), Game::GameName().c_str(),
	      base_ca_.CardAbstractionName().c_str(),
	      Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	      base_ba_.BettingAbstractionName().c_str(),
	      base_cc_.CFRConfigName().c_str(),
	      endgame_ca_.CardAbstractionName().c_str(),
	      endgame_ba_.BettingAbstractionName().c_str(),
	      endgame_cc_.CFRConfigName().c_str(),
	      ResolvingMethodName(method_), solve_street_, target_street_);
      for (unsigned int gbd = 0; gbd < num_boards; ++gbd) {
	CFRValues endgame_sumprobs(nullptr, true, endgame_streets,
				   subtree, gbd, target_street_,
				   endgame_ca_, *endgame_buckets_,
				   endgame_compressed_streets);
	endgame_sumprobs.Read(read_dir, endgame_it_, subtree->Root(),
			      base_subtree_nt, kMaxUInt);
	merged_sumprobs_->MergeInto(endgame_sumprobs, gbd, endgame_node,
				    subtree->Root(), *endgame_buckets_,
				    Game::MaxStreet());
      }
      
      delete subtree;
      delete [] endgame_streets;
      delete [] endgame_compressed_streets;

      return;
    }
  }
  unsigned int num_succs = base_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    WalkTrunk(base_node->IthSucc(s), endgame_node->IthSucc(s), st);
  }
}

void Assembler::Go(void) {
  WalkTrunk(base_betting_tree_->Root(), endgame_betting_tree_->Root(),
	    base_betting_tree_->Root()->Street());
  char dir[500];
  sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::NewCFRBase(),
	  Game::GameName().c_str(), merged_ca_.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  endgame_ba_.BettingAbstractionName().c_str(),
	  merged_cc_.CFRConfigName().c_str());
  merged_sumprobs_->Write(dir, endgame_it_, endgame_betting_tree_->Root(),
			  endgame_betting_tree_->Root()->NonterminalID(),
			  kMaxUInt);
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card params> "
	  "<endgame card params> <merged card params> <base betting params> "
	  "<endgame betting params> <base CFR params> <endgame CFR params> "
	  "<merged CFR params> <solve street> <target street> "
	  "<target street threshold> <base it> <endgame it> <method>\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 16) Usage(argv[0]);
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
  unique_ptr<Params> merged_card_params = CreateCardAbstractionParams();
  merged_card_params->ReadFromFile(argv[4]);
  unique_ptr<CardAbstraction>
    merged_card_abstraction(new CardAbstraction(*merged_card_params));
  unique_ptr<Params> base_betting_params = CreateBettingAbstractionParams();
  base_betting_params->ReadFromFile(argv[5]);
  unique_ptr<BettingAbstraction>
    base_betting_abstraction(new BettingAbstraction(*base_betting_params));
  unique_ptr<Params> endgame_betting_params = CreateBettingAbstractionParams();
  endgame_betting_params->ReadFromFile(argv[6]);
  unique_ptr<BettingAbstraction>
    endgame_betting_abstraction(
		    new BettingAbstraction(*endgame_betting_params));
  unique_ptr<Params> base_cfr_params = CreateCFRParams();
  base_cfr_params->ReadFromFile(argv[7]);
  unique_ptr<CFRConfig>
    base_cfr_config(new CFRConfig(*base_cfr_params));
  unique_ptr<Params> endgame_cfr_params = CreateCFRParams();
  endgame_cfr_params->ReadFromFile(argv[8]);
  unique_ptr<CFRConfig>
    endgame_cfr_config(new CFRConfig(*endgame_cfr_params));
  unique_ptr<Params> merged_cfr_params = CreateCFRParams();
  merged_cfr_params->ReadFromFile(argv[9]);
  unique_ptr<CFRConfig>
    merged_cfr_config(new CFRConfig(*merged_cfr_params));

  unsigned int solve_street, target_street, base_it, endgame_it;
  unsigned int target_street_threshold;
  if (sscanf(argv[10], "%u", &solve_street) != 1)  Usage(argv[0]);
  if (sscanf(argv[11], "%u", &target_street) != 1) Usage(argv[0]);
  if (sscanf(argv[12], "%u", &target_street_threshold) != 1) Usage(argv[0]);
  if (sscanf(argv[13], "%u", &base_it) != 1)       Usage(argv[0]);
  if (sscanf(argv[14], "%u", &endgame_it) != 1)    Usage(argv[0]);
  string m = argv[15];
  ResolvingMethod method;
  if (m == "unsafe")         method = ResolvingMethod::UNSAFE;
  else if (m == "cfrd")      method = ResolvingMethod::CFRD;
  else if (m == "maxmargin") method = ResolvingMethod::MAXMARGIN;
  else if (m == "combined")  method = ResolvingMethod::COMBINED;
  else                       Usage(argv[0]);

  BettingTree *base_betting_tree =
    BettingTree::BuildTree(*base_betting_abstraction);
  BettingTree *endgame_betting_tree =
    BettingTree::BuildTree(*endgame_betting_abstraction);
  BoardTree::Create();

  Assembler assembler(base_betting_tree, endgame_betting_tree, solve_street,
		      target_street, target_street_threshold, base_it,
		      endgame_it, *base_card_abstraction,
		      *endgame_card_abstraction, *merged_card_abstraction,
		      *base_betting_abstraction,
		      *endgame_betting_abstraction, *base_cfr_config,
		      *endgame_cfr_config, *merged_cfr_config, method);
  assembler.Go();
}
