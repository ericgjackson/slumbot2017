// Restructures the probs for use at runtime by the bot.
// Does different things for the trunk and the river subgames that will be
// resolved.
// In the trunk, quantizes probs to one byte.
// In the endgame, purifies and encodes the best succ at every information
// set with two bits.
//
// For the P1 target system, I should save both P0 and P1 pure probs on
// the river.  Needed to calculate T values.

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
#include "cfr_value_type.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "game_params.h"
#include "hand_tree.h"
#include "io.h"
#include "params.h"

class Restructurer {
public:
  Restructurer(const CardAbstraction &card_abstraction,
	       const BettingAbstraction &betting_abstraction,
	       const CFRConfig &cfr_config, const Buckets &buckets,
	       BettingTree *betting_tree, unsigned int it, unsigned int asym_p,
	       unsigned int subgame_st, bool current, bool turn_half_byte);
  ~Restructurer(void);
  void Go(void);
private:
  void WalkSubgame(Node *node);
  void WalkTrunk(Node *node, bool ***seen);
  
  const CardAbstraction &card_abstraction_;
  const BettingAbstraction &betting_abstraction_;
  const CFRConfig &cfr_config_;
  const Buckets &buckets_;
  BettingTree *betting_tree_;
  unsigned int it_;
  unsigned int asym_p_;
  unsigned int subgame_st_;
  bool current_;
  bool turn_half_byte_;
  CFRValueType **value_types_;
  Reader ***readers_;
  Writer ***writers_;
};

Restructurer::Restructurer(const CardAbstraction &card_abstraction,
			   const BettingAbstraction &betting_abstraction,
			   const CFRConfig &cfr_config, const Buckets &buckets,
			   BettingTree *betting_tree, unsigned int it,
			   unsigned int asym_p, unsigned int subgame_st,
			   bool current, bool turn_half_byte) :
  card_abstraction_(card_abstraction),
  betting_abstraction_(betting_abstraction), cfr_config_(cfr_config),
  buckets_(buckets) {
  betting_tree_ = betting_tree;
  it_ = it;
  asym_p_ = asym_p;
  subgame_st_ = subgame_st;
  current_ = current;
  turn_half_byte_ = turn_half_byte;
  char in_dir[500], out_dir[500], buf[500];
  sprintf(in_dir, "%s/%s.%u.%s.%u.%u.%u.%s.%s", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction.BettingAbstractionName().c_str(),
	  cfr_config.CFRConfigName().c_str());
  // Write to a new directory; note "r" at end
  sprintf(out_dir, "%s/%s.%u.%s.%u.%u.%u.%s.%sr", Files::OldCFRBase(),
	  Game::GameName().c_str(), Game::NumPlayers(),
	  card_abstraction.CardAbstractionName().c_str(),
	  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
	  betting_abstraction.BettingAbstractionName().c_str(),
	  cfr_config.CFRConfigName().c_str());
  if (betting_abstraction.Asymmetric()) {
    sprintf(buf, ".p%u", asym_p_);
    strcat(in_dir, buf);
    strcat(out_dir, buf);
  }
  Mkdir(out_dir);
  
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  value_types_ = new CFRValueType *[num_players];
  readers_ = new Reader **[num_players];
  writers_ = new Writer **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    readers_[p] = new Reader *[max_street + 1];
    writers_[p] = new Writer *[max_street + 1];
    value_types_[p] = new CFRValueType[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (st < subgame_st_ || ! current_) {
	sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.d", in_dir, st, it_, p);
	if (FileExists(buf)) {
	  value_types_[p][st] = CFR_DOUBLE;
	} else {
	  sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.i", in_dir, st, it_, p);
	  value_types_[p][st] = CFR_INT;
	}
      } else {
	sprintf(buf, "%s/regrets.x.0.0.%u.%u.p%u.i", in_dir, st, it_, p);
	if (FileExists(buf)) {
	  value_types_[p][st] = CFR_INT;
	} else {
	  sprintf(buf, "%s/regrets.x.0.0.%u.%u.p%u.c", in_dir, st, it_, p);
	  value_types_[p][st] = CFR_CHAR;
	}
      }
      readers_[p][st] = new Reader(buf);
      if (st >= subgame_st_) {
	// On subgame streets (river), encode best-succ from current
	// strategy with two bits.
	sprintf(buf, "%s/regrets.x.0.0.%u.%u.p%u.b", out_dir, st, it_, p);
      } else if (st == 2 && turn_half_byte_) {
	// On turn for heads-up, encode sumprobs with half-byte
	sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.h", out_dir, st, it_, p);
      } else {
	// On preflop and flop, encode sumprobs with one byte
	// Also turn for multiplayer
	sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.c", out_dir, st, it_, p);
      }
      writers_[p][st] = new Writer(buf);
    }
  }
}

Restructurer::~Restructurer(void) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! readers_[p][st]->AtEnd()) {
	fprintf(stderr, "p %u st %u reader not at end pos %lli sz %lli\n",
		p, st, readers_[p][st]->BytePos(),
		readers_[p][st]->FileSize());
      }
      delete readers_[p][st];
      delete writers_[p][st];
    }
    delete [] readers_[p];
    delete [] writers_[p];
  }
  delete [] readers_;
  delete [] writers_;
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] value_types_[p];
  }
  delete [] value_types_;
}

static void AddBit(unsigned int bit, unsigned char *current_byte,
		   unsigned int *current_bit, Writer *writer) {
  if (bit) {
    *current_byte |= (1 << *current_bit);
  }
  if (*current_bit == 0) {
    writer->WriteUnsignedChar(*current_byte);
    *current_byte = 0;
    *current_bit = 7;
  } else {
    --*current_bit;
  }
}

static void RecordBestSucc4(unsigned int best_s, unsigned char *current_byte,
			    unsigned int *current_bit, Writer *writer) {
  if (best_s == 0) {
    AddBit(0, current_byte, current_bit, writer);
    AddBit(0, current_byte, current_bit, writer);
  } else if (best_s == 1) {
    AddBit(0, current_byte, current_bit, writer);
    AddBit(1, current_byte, current_bit, writer);
  } else if (best_s == 2) {
    AddBit(1, current_byte, current_bit, writer);
    AddBit(0, current_byte, current_bit, writer);
  } else if (best_s == 3) {
    AddBit(1, current_byte, current_bit, writer);
    AddBit(1, current_byte, current_bit, writer);
  } else {
    fprintf(stderr, "Best succ out of bounds\n");
    exit(-1);
  }
}

void Restructurer::WalkSubgame(Node *node) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int pa = node->PlayerActing();
  if (num_succs > 1) {
    if (num_succs > 4) {
      fprintf(stderr, "Expect no more than 4 succs\n");
      exit(-1);
    }
    unsigned int st = node->Street();
    Writer *writer = writers_[pa][st];
    unsigned char current_byte = 0;
    unsigned int current_bit = 7;
    unsigned int num_buckets = buckets_.NumBuckets(st);
    if (num_buckets == 0) {
      fprintf(stderr, "Zero buckets\n");
      exit(-1);
    }
    for (unsigned int b = 0; b < num_buckets; ++b) {
      unsigned int best_succ = kMaxUInt;
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (value_types_[pa][st] == CFR_CHAR) {
	  unsigned char r = readers_[pa][st]->ReadUnsignedCharOrDie();
	  if (r == 0 && best_succ == kMaxUInt) {
	    best_succ = s;
	  }
	} else if (value_types_[pa][st] == CFR_INT) {
	  int r = readers_[pa][st]->ReadIntOrDie();
	  if (r == 0 && best_succ == kMaxUInt) {
	    best_succ = s;
	  }
	}
      }
      if (best_succ == kMaxUInt) {
	unsigned int nt = node->NonterminalID();
	fprintf(stderr, "No zero-succ regret?!?\n");
	fprintf(stderr, "st %u p %u pa %u nt %u b %u\n", st, asym_p_, pa, nt,
		b);
	fprintf(stderr, "File: %s\n",
		readers_[pa][st]->Filename().c_str());
	exit(-1);
      }
      RecordBestSucc4(best_succ, &current_byte, &current_bit, writer);
    }
    // Might need to flush last byte
    if (current_bit != 7) {
      writer->WriteUnsignedChar(current_byte);
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    WalkSubgame(node->IthSucc(s));
  }
}

void Restructurer::WalkTrunk(Node *node, bool ***seen) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  if (seen[st][pa][nt]) return;
  seen[st][pa][nt] = true;
  if (st == subgame_st_) {
    WalkSubgame(node);
    return;
  }
  unsigned int num_succs = node->NumSuccs();
  if (num_succs > 1) {
    // "nq" stands for "num quantized values".  On turn (for heads-up) we want
    // to quantize to a half-byte, which means 16 possible values.  On
    // preflop and flop, want to quantize to a byte, which means 256 possible
    // values.
    unsigned int nq;
    if (st == 2 && turn_half_byte_) {
      nq = 16;
    } else {
      nq = 256;
    }
    unsigned int dsi = node->DefaultSuccIndex();
    unsigned int num_holdings;
    if (buckets_.None(st)) {
      unsigned int num_boards = BoardTree::NumBoards(st);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      num_holdings = num_boards * num_hole_card_pairs;
    } else {
      num_holdings = buckets_.NumBuckets(st);
    }
    Writer *writer = writers_[pa][st];
    unique_ptr<double []> dsps(new double[num_succs]);
    unique_ptr<unsigned int []> uisps(new unsigned int[num_succs]);
    unique_ptr<double []> d_probs(new double[num_succs]);
    unique_ptr<unsigned char []> q_probs(
		       new unsigned char[num_holdings * num_succs]);
    double d_sum;
    for (unsigned int h = 0; h < num_holdings; ++h) {
      if (value_types_[pa][st] == CFR_DOUBLE) {
	d_sum = 0;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  dsps[s] = readers_[pa][st]->ReadDoubleOrDie();
	  if (dsps[s] < 0) {
	    fprintf(stderr, "Negative dsp?!?\n");
	    exit(-1);
	  }
	  d_sum += dsps[s];
	}
	if (d_sum > 0) {
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    if (dsps[s] > 0) d_probs[s] = dsps[s] / d_sum;
	    else             d_probs[s] = 0;
	    if (d_probs[s] > 1.0) {
	      fprintf(stderr, "d_prob > 1?!?\n");
	      exit(-1);
	    }
	  }
	}
      } else {
	unsigned long long int sum = 0;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  uisps[s] = readers_[pa][st]->ReadUnsignedIntOrDie();
	  sum += uisps[s];
	}
	d_sum = sum;
	if (sum > 0) {
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    if (uisps[s] > 0) d_probs[s] = uisps[s] / d_sum;
	    else              d_probs[s] = 0;
	  }
	}
      }
      if (d_sum == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  unsigned int i = h * num_succs + s;
	  if (s == dsi) q_probs[i] = nq - 1;
	  else          q_probs[i] = 0;
	}
      } else {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  unsigned int i = h * num_succs + s;
	  if (d_probs[s] == 0) {
	    q_probs[i] = 0;
	  } else {
	    unsigned int qp = d_probs[s] * nq;
	    if (qp > nq) {
	      fprintf(stderr, "qp > nq?!?  dprob %f qp %u nq %u st %u\n",
		      d_probs[s], qp, nq, st);
	      exit(-1);
	    } else if (qp == nq) {
	      qp = nq - 1;
	    }
	    q_probs[i] = qp;
	  }
	}
      }
      unsigned int qsum = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	unsigned int i = h * num_succs + s;
	qsum += q_probs[i];
      }
      // The sum of the quantized probs should add to 1 (i.e., to 255 or
      // 15).
      if (qsum < nq - 1) {
	// If the sum is too low, add to the highest probability action.
	unsigned int delta = (nq - 1) - qsum;
	unsigned int i = h * num_succs;
	unsigned int max_qp = q_probs[i];
	unsigned int max_s = 0;
	for (unsigned int s = 1; s < num_succs; ++s) {
	  unsigned int i = h * num_succs;
	  if (q_probs[i] > max_qp) {
	    max_qp = q_probs[i];
	    max_s = s;
	  }
	}
	i = h * num_succs + max_s;
	q_probs[i] += delta;
      } else {
	// If the sum is too high, repeatedly subtract one from the lowest
	// probability action until the sum of probs is correct.
	while (qsum > nq - 1) {
	  unsigned int min_qp = nq;
	  unsigned int min_s = kMaxUInt;
	  for (unsigned int s = 0; s < num_succs; ++s) {
	    unsigned int i = h * num_succs + s;
	    unsigned int qp = q_probs[i];
	    if (qp > 0 && qp < min_qp) {
	      min_s = s;
	      min_qp = qp;
	    }
	  }
	  unsigned int i = h * num_succs + min_s;
	  q_probs[i] -= 1;
	  --qsum;
	}
      }
    }
    if (st == 2 && turn_half_byte_) {
      // Half-byte per probability on the turn.
      unsigned char last = 0;
      unsigned int i = 0;
      for (unsigned int h = 0; h < num_holdings; ++h) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  i = h * num_succs + s;
	  if (i % 2 == 0) {
	    last = q_probs[i];
	  } else {
	    unsigned char c = (last << 4) | q_probs[i];
	    writer->WriteUnsignedChar(c);
	  }
	}
      }
      if (i % 2 == 0) {
	writer->WriteUnsignedChar(last << 4);
      }
    } else {
      // One byte per probability
      for (unsigned int h = 0; h < num_holdings; ++h) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  unsigned int i = h * num_succs + s;
	  writer->WriteUnsignedChar(q_probs[i]);
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    WalkTrunk(node->IthSucc(s), seen);
  }
}

void Restructurer::Go(void) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  bool ***seen = new bool **[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    seen[st] = new bool *[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      unsigned int num_nt = betting_tree_->NumNonterminals(p, st);
      seen[st][p] = new bool[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	seen[st][p][i] = false;
      }
    }
  }
  WalkTrunk(betting_tree_->Root(), seen);
  for (unsigned int st = 0; st <= max_street; ++st) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete [] seen[st][p];
    }
    delete [] seen[st];
  }
  delete [] seen;
}

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <game params> <card params> <betting params> "
	  "<CFR params> <it> <subgame st> [avg|current] [byte|halfbyte] "
	  "(player)\n",
	  prog_name);
  fprintf(stderr, "\n");
  fprintf(stderr, "Use large value for subgame street if we will not be "
	  "resolving subgames.\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "\"current\" or \"avg\" signifies whether we use the "
	  "current strategy (from regrets) or the avg strategy (from "
	  "sumprobs)\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Byte or halfbyte expressed how we want to quantize turn "
	  "probs.\n");
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 9 && argc != 10) Usage(argv[0]);
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
  unsigned int it;
  if (sscanf(argv[5], "%u", &it) != 1) Usage(argv[0]);
  unsigned int subgame_st;
  if (sscanf(argv[6], "%u", &subgame_st) != 1) Usage(argv[0]);
  string c = argv[7];
  bool current;
  if (c == "current")  current = true;
  else if (c == "avg") current = false;
  else                 Usage(argv[0]);
  bool turn_half_byte;
  string thb = argv[8];
  if (thb == "byte")          turn_half_byte = false;
  else if (thb == "halfbyte") turn_half_byte = true;
  else                        Usage(argv[0]);
  unsigned int p = kMaxUInt;
  if (betting_abstraction->Asymmetric()) {
    if (argc == 9) {
      fprintf(stderr, "Too few arguments for asymmetric system\n");
      Usage(argv[0]);
    }
    if (sscanf(argv[9] + 1, "%u", &p) != 1) Usage(argv[0]);
  } else {
    if (argc == 10) {
      fprintf(stderr, "Too many arguments for symmetric system\n");
      Usage(argv[0]);
    }
  }

  unique_ptr<BettingTree> betting_tree;
  if (betting_abstraction->Asymmetric()) {
    betting_tree.reset(
		  BettingTree::BuildAsymmetricTree(*betting_abstraction, p));
  } else {
    betting_tree.reset(BettingTree::BuildTree(*betting_abstraction));
  }

  Buckets buckets(*card_abstraction, true);
  BoardTree::Create();

  Restructurer restructurer(*card_abstraction, *betting_abstraction,
			    *cfr_config, buckets, betting_tree.get(),
			    it, p, subgame_st, current, turn_half_byte);
  restructurer.Go();
}
