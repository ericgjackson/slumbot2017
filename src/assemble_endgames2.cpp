// This version of assemble_endgames2.cpp is written to work with
// endgames solved by solve_all_endgames2.cpp.  Main difference is that
// files are stored in different locations.
//
// The betting tree will typically be different depending on whether P0 or
// P1 is the target player.
//
// Do I need to create and fill a merged sumprobs object?  Maybe I can
// just write the merged files out directly from the endgame sumprobs
// object.  Well, no, we have one endgame sumprobs object for each board.
//
// Do I need to create a betting tree for the entire merged system?  That's
// what endgame_betting_tree is.  Won't I need it for the bot?  If so, I
// can assume it is possible to create here.

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
#include "cfr.h"
#include "eg_cfr.h" // ResolvingMethod
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "io.h"
#include "params.h"
#include "split.h"

class Assembler {
public:
  Assembler(BettingTree *base_betting_tree, unsigned int solve_street,
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
  void ReadEndgame(Node *node, unsigned int p, unsigned int gbd,
		   unsigned int root_bd_st, unsigned int root_bd,
		   const string &action_sequence, CFRValues *endgame_sumprobs,
		   unsigned int last_st);
  void WalkTrunk(Node *base_node, Node **endgame_nodes,
		 const string &action_sequence);

  BettingTree *base_betting_tree_;
  BettingTree **endgame_betting_trees_;
  unsigned int solve_street_;
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
  CFRValues **merged_sumprobs_;
  unique_ptr<Buckets> endgame_buckets_;
  Writer ****writers_;
  void ****compressors_;
};

Assembler::Assembler(BettingTree *base_betting_tree,
		     unsigned int solve_street,  unsigned int base_it,
		     unsigned int endgame_it,
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
  base_betting_tree_ = base_betting_tree;
  solve_street_ = solve_street;
  base_it_ = base_it;
  endgame_it_ = endgame_it;
  method_ = method;

  unsigned int num_players = Game::NumPlayers();
  endgame_betting_trees_ = new BettingTree *[num_players];
  if (endgame_ba_.Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      endgame_betting_trees_[p] =
	BettingTree::BuildAsymmetricTree(endgame_ba_, p);
    }
  } else {
    endgame_betting_trees_[0] = 
      BettingTree::BuildTree(endgame_ba_);
    for (unsigned int p = 1; p < num_players; ++p) {
      endgame_betting_trees_[p] = endgame_betting_trees_[0];
    }
  }
  
  DeleteOldFiles(merged_ca_, endgame_ba_, merged_cc_, endgame_it_);

  endgame_buckets_.reset(new Buckets(endgame_ca_, true));
  
  unsigned int max_street = Game::MaxStreet();
  bool *base_streets = new bool[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    base_streets[st] = st < solve_street;
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

  CFRValues base_sumprobs(nullptr, true, base_streets, base_betting_tree_,
			  0, 0, base_ca_, base_buckets,
			  base_compressed_streets);
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

  unique_ptr<bool []> merged_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    merged_streets[st] = st >= solve_street_;
  }
  Buckets merged_buckets(merged_ca_, true);

  unique_ptr<bool []> merged_compressed_streets(new bool[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    merged_compressed_streets[st] = false;
  }
  const vector<unsigned int> &mcsv = merged_cc_.CompressedStreets();
  unsigned int num_mcsv = mcsv.size();
  for (unsigned int i = 0; i < num_mcsv; ++i) {
    unsigned int st = mcsv[i];
    merged_compressed_streets[st] = true;
  }

  merged_sumprobs_ = new CFRValues *[num_players];
  writers_ = new Writer ***[num_players];
  compressors_ = new void ***[num_players];
  if (endgame_ba_.Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      merged_sumprobs_[p] = new CFRValues(nullptr, true, merged_streets.get(),
					  endgame_betting_trees_[p], 0, 0,
					  merged_ca_, merged_buckets,
					  merged_compressed_streets.get());
      writers_[p] =
	merged_sumprobs_[p]->InitializeWriters(write_dir, endgame_it_, 0,
					       0, kMaxUInt, &compressors_[p]);
    }
  } else {
    merged_sumprobs_[0] = new CFRValues(nullptr, true, merged_streets.get(),
					endgame_betting_trees_[0], 0, 0,
					merged_ca_, merged_buckets,
					merged_compressed_streets.get());
    writers_[0] =
      merged_sumprobs_[0]->InitializeWriters(write_dir, endgame_it_, 0,
					     0, kMaxUInt, &compressors_[0]);
    for (unsigned int p = 1; p < num_players; ++p) {
      merged_sumprobs_[p] = merged_sumprobs_[0];
      writers_[p] = writers_[0];
      compressors_[p] = compressors_[0];
    }
  }

  // Choosing not to merge preflop into merged_sumprobs_, but we could
}

Assembler::~Assembler(void) {
  unsigned int num_players = Game::NumPlayers();
  if (endgame_ba_.Asymmetric()) {
    for (unsigned int p = 0; p < num_players; ++p) {
      merged_sumprobs_[p]->DeleteWriters(writers_[p], compressors_[p]);
      delete merged_sumprobs_[p];
      delete endgame_betting_trees_[p];
    }
  } else {
    merged_sumprobs_[0]->DeleteWriters(writers_[0], compressors_[0]);
    delete merged_sumprobs_[0];
    delete endgame_betting_trees_[0];
  }
  delete [] writers_;
  delete [] compressors_;
  delete [] merged_sumprobs_;
  delete [] endgame_betting_trees_;
}

void Assembler::ReadEndgame(Node *node, unsigned int p, unsigned int gbd,
			    unsigned int root_bd_st, unsigned int root_bd,
			    const string &action_sequence,
			    CFRValues *endgame_sumprobs,
			    unsigned int last_st) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > last_st) {
    unsigned int ngbd_begin = BoardTree::SuccBoardBegin(last_st, gbd, st);
    unsigned int ngbd_end = BoardTree::SuccBoardEnd(last_st, gbd, st);
    for (unsigned int ngbd = ngbd_begin; ngbd < ngbd_end; ++ngbd) {
      ReadEndgame(node, p, ngbd, root_bd_st, root_bd, action_sequence,
		  endgame_sumprobs, st);
    }
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  if (node->PlayerActing() == p) {
    char dir[500], dir2[500], dir3[500], filename[500];
    sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(),base_ca_.CardAbstractionName().c_str(),
	    Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	    base_ba_.BettingAbstractionName().c_str(),
	    base_cc_.CFRConfigName().c_str());
    if (base_ba_.Asymmetric()) {
      char buf[100];
      sprintf(buf, ".p%u", p);
      strcat(dir, buf);
    }
    sprintf(dir2, "%s/endgames.%s.%s.%s.%s.p%u.p%u", dir,
	    endgame_ca_.CardAbstractionName().c_str(),
	    endgame_ba_.BettingAbstractionName().c_str(),
	    endgame_cc_.CFRConfigName().c_str(),
	    ResolvingMethodName(method_), p, p);
    Mkdir(dir2);
    
    if (action_sequence == "") {
      fprintf(stderr, "Empty action sequence not allowed\n");
      exit(-1);
    }
    sprintf(dir3, "%s/%s", dir2, action_sequence.c_str());
    Mkdir(dir3);

    sprintf(filename, "%s/%u", dir3, gbd);
    Reader reader(filename);
    
    // Assume doubles in file
    // Also assume endgame solving is unabstracted
    // We write only one board's data per file, even on streets later than
    // solve street.
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(node->Street());
    unsigned int lbd = BoardTree::LocalIndex(root_bd_st, root_bd, st, gbd);
    unsigned int offset = lbd * num_hole_card_pairs * num_succs;
    endgame_sumprobs->ReadNode(node, &reader, nullptr, num_hole_card_pairs,
			       false, offset);
#if 0
    if (gbd == 1 && st == 2 && node->NonterminalID() == 0 && p == 0 &&
	node->PlayerActing() == 0 && action_sequence == "cccc") {
      fprintf(stderr, "%s nt %u gbd 1 lbd %u\n", action_sequence.c_str(),
	      node->NonterminalID(), lbd);
      double *d_values;
      endgame_sumprobs->Values(p, st, node->NonterminalID(), &d_values);
      fprintf(stderr, "sps %f %f lbd %u nhcp %u ns %u offset %u\n",
	      d_values[offset + 0], d_values[offset + 1], lbd,
	      num_hole_card_pairs, num_succs, offset);
    }
#endif
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = node->ActionName(s);
    ReadEndgame(node->IthSucc(s), p, gbd, root_bd_st, root_bd,
		action_sequence + action, endgame_sumprobs, st);
  }
}

// Do I even need endgame node any more?
void Assembler::WalkTrunk(Node *base_node, Node **endgame_nodes,
			  const string &action_sequence) {
  if (base_node->Terminal()) return;
  unsigned int num_players = Game::NumPlayers();
  unsigned int st = base_node->Street();
  if (st == solve_street_) {
    if (action_sequence == "") {
      fprintf(stderr, "Empty action sequence not allowed\n");
      exit(-1);
    }
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int max_street = Game::MaxStreet();
    unique_ptr<bool []> endgame_streets(new bool[max_street + 1]);
    for (unsigned int st = 0; st <= max_street; ++st) {
      endgame_streets[st] = st >= solve_street_;
    }
    for (unsigned int p = 0; p < num_players; ++p) {
      // I need a subtree to get the right number of nonterminals, and
      // to have a dense nonterminal indexing stored in the nodes.
      BettingTree *subtree = BettingTree::BuildSubtree(endgame_nodes[p]);
      unique_ptr<bool []> players(new bool[2]);
      for (unsigned int p1 = 0; p1 < num_players; ++p1) {
	players[p1] = (p1 == p);
      }
      for (unsigned int gbd = 0; gbd < num_boards; ++gbd) {
	CFRValues endgame_sumprobs(players.get(), true, endgame_streets.get(),
				   subtree, gbd, solve_street_,
				   endgame_ca_, *endgame_buckets_,
				   nullptr);
	ReadEndgame(subtree->Root(), p, gbd, st, gbd, action_sequence,
		    &endgame_sumprobs, st);
	if (endgame_ba_.Asymmetric()) {
	  merged_sumprobs_[p]->MergeInto(endgame_sumprobs, gbd,
					 endgame_nodes[p], subtree->Root(),
					 *endgame_buckets_, max_street);
	} else {
	  merged_sumprobs_[0]->MergeInto(endgame_sumprobs, gbd,
					 endgame_nodes[p], subtree->Root(),
					 *endgame_buckets_, max_street);
	}
      }
      delete subtree;
      if (endgame_ba_.Asymmetric()) {
	merged_sumprobs_[p]->Write(endgame_nodes[p], writers_[p],
				   compressors_[p]);
	merged_sumprobs_[p]->DeleteBelow(endgame_nodes[p]);
      }
    }
    if (! endgame_ba_.Asymmetric()) {
      merged_sumprobs_[0]->Write(endgame_nodes[0], writers_[0],
				 compressors_[0]);
      merged_sumprobs_[0]->DeleteBelow(endgame_nodes[0]);
    }
    return;
  }
  unsigned int num_succs = base_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    Node **new_endgame_nodes = new Node *[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      new_endgame_nodes[p] = endgame_nodes[p]->IthSucc(s);
    }
    string action = base_node->ActionName(s);
    WalkTrunk(base_node->IthSucc(s), new_endgame_nodes,
	      action_sequence + action);
    delete [] new_endgame_nodes;
  }
}

#if 0
void Assembler::WalkTrunk(Node *base_node, Node *endgame_node,
			  const string &action_sequence) {
  if (base_node->Terminal()) return;
  unsigned int st = base_node->Street();
  if (st == solve_street_) {
    if (action_sequence == "") {
      fprintf(stderr, "Empty action sequence not allowed\n");
      exit(-1);
    }
    unsigned int num_boards = BoardTree::NumBoards(st);
    unsigned int base_subtree_nt = base_node->NonterminalID();
    fprintf(stderr, "Base subtree NT: %u\n", base_subtree_nt);
    BettingTree *subtree = BettingTree::BuildSubtree(endgame_node);
    unsigned int max_street = Game::MaxStreet();
    bool *endgame_streets = new bool[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      endgame_streets[st] = st >= solve_street_;
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
    char dir[500], dir2[500], dir3[500], dir4[500];
    sprintf(dir, "%s/%s.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	    Game::GameName().c_str(), base_ca_.CardAbstractionName().c_str(),
	    Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	    base_ba_.BettingAbstractionName().c_str(),
	    base_cc_.CFRConfigName().c_str());
#if 0
    // Not supported yet?
    if (base_ba_.Asymmetric()) {
      char buf[100];
      sprintf(buf, ".p%u", target_p_);
      strcat(dir, buf);
    }
#endif
    unsigned int num_players = Game::NumPlayers();
    for (unsigned int p = 0; p < num_players; ++p) {
      unique_ptr<bool []> players(new bool[2]);
      for (unsigned int p1 = 0; p1 < num_players; ++p1) {
	players[p1] = (p == p1);
      }
      sprintf(dir2, "%s/endgames.%s.%s.%s.%s.%u", dir,
	      endgame_ca_.CardAbstractionName().c_str(),
	      endgame_ba_.BettingAbstractionName().c_str(),
	      endgame_cc_.CFRConfigName().c_str(),
	      ResolvingMethodName(method_), p);
      sprintf(dir3, "%s/%s", dir2, action_sequence.c_str());
      for (unsigned int gbd = 0; gbd < num_boards; ++gbd) {
	sprintf(dir4, "%s/%u", dir3, gbd);
	CFRValues endgame_sumprobs(players.get(), true, endgame_streets,
				   subtree, gbd, solve_street_,
				   endgame_ca_, *endgame_buckets_,
				   endgame_compressed_streets);
	// Pass in zero for the nonterminal ID of the base.  We don't need it
	// to identify the file.
	endgame_sumprobs.Read(dir4, endgame_it_, subtree->Root(), 0,
			      kMaxUInt);
	merged_sumprobs_->MergeInto(endgame_sumprobs, gbd, endgame_node,
				    subtree->Root(), *endgame_buckets_,
				    max_street);
      }
    }

    delete subtree;
    delete [] endgame_streets;
    delete [] endgame_compressed_streets;

    merged_sumprobs_->Write(endgame_node, kMaxUInt, kMaxUInt, writers_,
			    compressors_);
    merged_sumprobs_->DeleteBelow(endgame_node);
    
    return;
  }
  unsigned int num_succs = base_node->NumSuccs();
  for (unsigned int s = 0; s < num_succs; ++s) {
    string action = base_node->ActionName(s);
    WalkTrunk(base_node->IthSucc(s), endgame_node->IthSucc(s),
	      action_sequence + action);
  }
}
#endif

void Assembler::Go(void) {
  unsigned int num_players = Game::NumPlayers();
  Node **endgame_nodes = new Node *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    endgame_nodes[p] = endgame_betting_trees_[p]->Root();
  }
  WalkTrunk(base_betting_tree_->Root(), endgame_nodes, "");
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <base card params> "
	  "<endgame card params> <merged card params> <base betting params> "
	  "<endgame betting params> <base CFR params> <endgame CFR params> "
	  "<merged CFR params> <solve street> <base it> "
	  "<endgame it> <method>\n",
	  prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 14) Usage(argv[0]);
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

  unsigned int solve_street, base_it, endgame_it;
  if (sscanf(argv[10], "%u", &solve_street) != 1)  Usage(argv[0]);
  if (sscanf(argv[11], "%u", &base_it) != 1)       Usage(argv[0]);
  if (sscanf(argv[12], "%u", &endgame_it) != 1)    Usage(argv[0]);
  string m = argv[13];
  ResolvingMethod method;
  if (m == "unsafe")         method = ResolvingMethod::UNSAFE;
  else if (m == "cfrd")      method = ResolvingMethod::CFRD;
  else if (m == "maxmargin") method = ResolvingMethod::MAXMARGIN;
  else if (m == "combined")  method = ResolvingMethod::COMBINED;
  else                       Usage(argv[0]);

  BettingTree *base_betting_tree =
    BettingTree::BuildTree(*base_betting_abstraction);
  BoardTree::Create();

  Assembler assembler(base_betting_tree, solve_street, base_it, endgame_it,
		      *base_card_abstraction, *endgame_card_abstraction,
		      *merged_card_abstraction, *base_betting_abstraction,
		      *endgame_betting_abstraction, *base_cfr_config,
		      *endgame_cfr_config, *merged_cfr_config, method);
  assembler.Go();
}
