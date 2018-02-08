// Another test: any action taken with probability greater than threshold
// should have EV that is within threshold of best action.

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
#include "cards.h"
#include "cfr_config.h"
#include "cfr_params.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "joint_reach_probs.h"
#include "params.h"

class Walker {
public:
  Walker(const CardAbstraction &card_abstraction,
	 const BettingAbstraction &betting_abstraction,
	 const CFRConfig &cfr_config, const Buckets &buckets, unsigned int it,
	 unsigned int final_st);
  void Go(void);
private:
  void Walk(Node *node, string *action_sequences);
  
  const Buckets &buckets_;
  unsigned int it_;
  unsigned int final_st_;
  unique_ptr<BettingTree> betting_tree_;
  char dir_[500];
  Reader ***strategy_readers_;
  CFRValueType **value_types_;
  unique_ptr<JointReachProbs> joint_reach_probs_;
  unsigned int num_consistent_;
  unsigned int num_inconsistent_;
  unsigned int num_skipped_;
  unsigned int num_consistent2_;
  unsigned int num_inconsistent2_;
};

Reader *InitializeReader(const char *dir, unsigned int p, unsigned int st,
			 unsigned int it, CFRValueType *value_type) {
  char buf[500];
  
  sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.d", dir, st, it, p);
  if (FileExists(buf)) {
    *value_type = CFR_DOUBLE;
  } else {
    sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.i", dir, st, it, p);
    if (FileExists(buf)) {
      *value_type = CFR_INT;
    } else {
      sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.c", dir, st, it, p);
      if (FileExists(buf)) {
	*value_type = CFR_CHAR;
      } else {
	sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.s", dir, st, it, p);
	if (FileExists(buf)) {
	  *value_type = CFR_SHORT;
	} else {
	  fprintf(stderr, "Couldn't find file\n");
	  fprintf(stderr, "buf: %s\n", buf);
	  exit(-1);
	}
      }
    }
  }
  Reader *reader = new Reader(buf);
  return reader;
}

Walker::Walker(const CardAbstraction &card_abstraction,
	       const BettingAbstraction &betting_abstraction,
	       const CFRConfig &cfr_config, const Buckets &buckets,
	       unsigned int it, unsigned int final_st) :
  buckets_(buckets) {
  it_ = it;
  final_st_ = final_st;
  
  if (betting_abstraction.Asymmetric()) {
    fprintf(stderr, "Asymmetric not supported yet\n");
    exit(-1);
#if 0
    betting_tree_.reset(
		 BettingTree::BuildAsymmetricTree(betting_abstraction, p));
#endif
  } else {
    betting_tree_.reset(BettingTree::BuildTree(betting_abstraction));
  }

  sprintf(dir_, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction.BettingAbstractionName().c_str(),
	  cfr_config.CFRConfigName().c_str());

  unsigned int num_players = Game::NumPlayers();
  strategy_readers_ = new Reader **[num_players];
  value_types_ = new CFRValueType *[num_players];
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int p = 0; p < num_players; ++p) {
    strategy_readers_[p] = new Reader *[max_street + 1];
    value_types_[p] = new CFRValueType[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      strategy_readers_[p][st] =
	InitializeReader(dir_, p, st, it, &value_types_[p][st]);
    }
  }

  joint_reach_probs_.reset(new JointReachProbs(card_abstraction,
					       betting_abstraction, cfr_config,
					       buckets.NumBuckets(), it,
					       final_st));
  num_consistent_ = 0;
  num_inconsistent_ = 0;
  num_skipped_ = 0;
  num_consistent2_ = 0;
  num_inconsistent2_ = 0;
}

Reader *InitializeCVReader(const char *dir, string *action_sequences,
			   unsigned int p, unsigned int st,
			   unsigned int it) {
  char buf[500];
  
  sprintf(buf, "%s/sbcfrs.%u.p%u/%u", dir,
	  it, p, st);
  string path = buf;
  for (unsigned int st1 = 0; st1 <= st; ++st1) {
    path += "/";
    path += action_sequences[st1];
  }
  Reader *reader = new Reader(path.c_str());
  return reader;
}

void Walker::Walk(Node *node, string *action_sequences) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  if (st > final_st_) return;
  unsigned int pa = node->PlayerActing();
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    Reader *cv_reader =
      InitializeCVReader(dir_, action_sequences, pa, st, it_);
    unsigned int dsi = node->DefaultSuccIndex();
    unsigned int num_buckets = buckets_.NumBuckets(st);
    unique_ptr<double []> probs(new double[num_succs]);
    unique_ptr<unsigned int []> uisps(new unsigned int[num_succs]);
    unique_ptr<double []> dsps(new double[num_succs]);
    unique_ptr<float []> cvs(new float[num_succs]);
    unsigned int nt = node->NonterminalID();
    for (unsigned int b = 0; b < num_buckets; ++b) {
      double sum = 0;
      if (value_types_[pa][st] == CFR_INT) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  unsigned int sp = strategy_readers_[pa][st]->ReadUnsignedIntOrDie();
	  uisps[s] = sp;
	  sum += sp;
	}
	if (sum == 0) {
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    probs[s] = (s == dsi ? 1.0 : 0);
	  }
	} else {
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    probs[s] = ((double)uisps[s]) / sum;
	  }
	}
      } else if (value_types_[pa][st] == CFR_DOUBLE) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  double sp = strategy_readers_[pa][st]->ReadDoubleOrDie();
	  dsps[s] = sp;
	  sum += sp;
	}
	if (sum == 0) {
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    probs[s] = (s == dsi ? 1.0 : 0);
	  }
	} else {
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    probs[s] = dsps[s] / sum;
	  }
	}
      } else {
	fprintf(stderr, "Expected int or double sumprobs\n");
	exit(-1);
      }
      unsigned int max_cv_s = 0;
      float max_cv = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	float cv = cv_reader->ReadFloatOrDie();
	cvs[s] = cv;
	if (s == 0 || cv > max_cv) {
	  max_cv = cv;
	  max_cv_s = s;
	}
      }
      unsigned int max_s = 0;
      double max_prob = probs[0];
      for (unsigned int s = 1; s < num_succs; ++s) {
	if (probs[s] > max_prob) {
	  max_s = s;
	  max_prob = probs[s];
	}
      }
      // double jrp = joint_reach_probs_->JointReachProb(pa, st, nt, b);
      float our_rp = joint_reach_probs_->OurReachProb(pa, st, nt, b);
      float opp_rp = joint_reach_probs_->OppReachProb(pa, st, nt, b);
      if (our_rp > 0.001 && opp_rp > 0.001) {
	if (b == 0) {
	  printf("Evaluating (b 0) ");
	  for (unsigned int st1 = 0; st1 <= st; ++st1) {
	    printf("%s", action_sequences[st1].c_str());
	    if (st1 < st) {
	      printf("/");
	    }
	  }
	  printf("\n");
	  fflush(stdout);
	}
	bool consistent = true;
	// Was 0.02
	if (max_s != max_cv_s && max_cv > cvs[max_s] + 0.025) {
	  double ratio = max_cv / cvs[max_s];
	  if (ratio < 0) ratio = -ratio;
	  // Was 0.9/1.1
	  if (ratio < 0.95 || ratio > 1.05) {
	    consistent = false;
	  }
	}
	if (consistent) {
	  ++num_consistent_;
	} else {
	  ++num_inconsistent_;
	  for (unsigned int st1 = 0; st1 <= st; ++st1) {
	    printf("%s", action_sequences[st1].c_str());
	    if (st1 < st) {
	      printf("/");
	    }
	  }
	  printf(" b %u max_s %u max_cv_s %u max_prob %f max_cv %f "
		 "cv[max_s] %f prob[max_cv_s] %f sum %f our_rp %f "
		 "opp_rp %f\n",
		 b, max_s, max_cv_s, max_prob, max_cv, cvs[max_s],
		 probs[max_cv_s], sum, our_rp, opp_rp);
	  fflush(stdout);
	}

	// Every action with non-trivial probability should have CV close
	// to max CV.
	bool consistent2 = true;
	unsigned int bad_s = kMaxUInt;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  if (s == max_cv_s) continue;
	  if (probs[s] >= 0.01) {
	    double my_cv = cvs[s];
	    if (max_cv > my_cv + 0.01) {
	      double ratio = max_cv / my_cv;
	      if (ratio > 1.01) {
		consistent2 = false;
		bad_s = s;
		break;
	      }
	    }
	  }
	}
	if (consistent2) {
	  ++num_consistent2_;
	} else {
	  ++num_inconsistent2_;
	  for (unsigned int st1 = 0; st1 <= st; ++st1) {
	    printf("%s", action_sequences[st1].c_str());
	    if (st1 < st) {
	      printf("/");
	    }
	  }
	  printf(" b %u bad_s %u max_cv_s %u bad_prob %f max_cv %f "
		 "cv[bad_s] %f sum %f our_rp %f opp_rp %f TWO\n",
		 b, bad_s, max_cv_s, probs[bad_s], max_cv, cvs[bad_s],
		 sum, our_rp, opp_rp);
	  fflush(stdout);
	}
	
      } else {
	++num_skipped_;
	if (b == 0) {
	  printf("Skipping (b 0) ");
	  for (unsigned int st1 = 0; st1 <= st; ++st1) {
	    printf("%s", action_sequences[st1].c_str());
	    if (st1 < st) {
	      printf("/");
	    }
	  }
	  printf("\n");
	  fflush(stdout);
	}
      }
    }
    delete cv_reader;
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    string old_as = action_sequences[st];
    action_sequences[st] += node->ActionName(s);
    Walk(node->IthSucc(s), action_sequences);
    action_sequences[st] = old_as;
  }
}

void Walker::Go(void) {
  string *action_sequences = new string[final_st_ + 1];
  for (unsigned int st = 0; st <= final_st_; ++st) {
    action_sequences[st] = "x";
  }
  Walk(betting_tree_->Root(), action_sequences);
  double num_eval = num_consistent_ + num_inconsistent_;
  double pct_eval = 100.0 * num_eval / (num_eval + num_skipped_);
  double pct_consistent = 100.0 * ((double)num_consistent_) / num_eval;
  fprintf(stderr, "%u consistent; %u inconsistent; %u skipped\n",
	  num_consistent_, num_inconsistent_, num_skipped_);
  fprintf(stderr, "%.2f%% evaluated\n", pct_eval);
  fprintf(stderr, "%.2f%% consistent (%u inconsistent)\n", pct_consistent,
	  num_inconsistent_);

  double num_eval2 = num_consistent2_ + num_inconsistent2_;
  double pct_consistent2 = 100.0 * ((double)num_consistent2_) / num_eval2;
  fprintf(stderr, "%.2f%% consistent2 (%u inconsistent2)\n", pct_consistent2,
	  num_inconsistent2_);
  
  delete [] action_sequences;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <final st>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 7) Usage(argv[0]);
  Files::Init();
  unique_ptr<Params> game_params = CreateGameParams();
  game_params->ReadFromFile(argv[1]);
  Game::Initialize(*game_params);
  unique_ptr<Params> card_params = CreateCardAbstractionParams();
  card_params->ReadFromFile(argv[2]);
  unique_ptr<CardAbstraction>
    card_abstraction(new CardAbstraction(*card_params));
  unique_ptr<Params> betting_params = CreateBettingAbstractionParams();
  betting_params->ReadFromFile(argv[3]);
  unique_ptr<BettingAbstraction>
    betting_abstraction(new BettingAbstraction(*betting_params));
  unique_ptr<Params> cfr_params = CreateCFRParams();
  cfr_params->ReadFromFile(argv[4]);
  unique_ptr<CFRConfig>
    cfr_config(new CFRConfig(*cfr_params));
  unsigned int it, final_st;
  if (sscanf(argv[5], "%u", &it) != 1)       Usage(argv[0]);
  if (sscanf(argv[6], "%u", &final_st) != 1) Usage(argv[0]);

  BoardTree::Create();

  Buckets buckets(*card_abstraction, true);
  Walker walker(*card_abstraction, *betting_abstraction, *cfr_config, buckets,
		it, final_st);
  walker.Go();
}
