#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "betting_abstraction.h"
#include "betting_tree.h"
#include "board_tree.h"
#include "card_abstraction.h"
#include "cfr_config.h"
#include "cfr_value_type.h"
#include "cfr_values_file.h"
#include "cfr_values.h"
#include "constants.h"
#include "files.h"
#include "game.h"
#include "io.h"

CFRValuesFile::CFRValuesFile(const bool *players, bool *streets,
			     const CardAbstraction &card_abstraction,
			     const BettingAbstraction &betting_abstraction,
			     const CFRConfig &cfr_config, unsigned int asym_p,
			     unsigned int it, unsigned int endgame_st,
			     const BettingTree *betting_tree,
			     const unsigned int *num_buckets) {
  unsigned int max_street = Game::MaxStreet();
  num_holdings_ = new unsigned int[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    unsigned int numb = num_buckets[st];
    if (numb > 0) {
      num_holdings_[st] = numb;
    } else {
      unsigned int num_boards = BoardTree::NumBoards(st);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      num_holdings_[st] = num_boards * num_hole_card_pairs;
    }
  }
  
  unsigned int num_players = Game::NumPlayers();
  value_types_ = new CFRValueType *[num_players];
  methods_ = new ProbMethod *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    value_types_[p] = new CFRValueType[max_street + 1];
    methods_[p] = new ProbMethod[max_street + 1];
  }

  readers_ = new Reader **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    if (players == nullptr || players[p]) {
      readers_[p] = new Reader *[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	if (streets == nullptr || streets[st]) {
	  char dir[500], buf[500];
	  sprintf(dir, "%s/%s.%u.%s.%i.%i.%i.%s.%s", Files::OldCFRBase(),
		  Game::GameName().c_str(), Game::NumPlayers(),
		  card_abstraction.CardAbstractionName().c_str(),
		  Game::NumRanks(), Game::NumSuits(), Game::MaxStreet(),
		  betting_abstraction.BettingAbstractionName().c_str(),
		  cfr_config.CFRConfigName().c_str());
	  if (betting_abstraction.Asymmetric()) {
	    char buf[100];
	    sprintf(buf, ".p%u", asym_p);
	    strcat(dir, buf);
	  }
	  // Look for files in a preferred order:
	  // 1) Double sumprobs.  (Get this when we solved endgames offline,
	  //    and then merged.)
	  // 2) Int sumprobs.  (Get this with base TCFR systems.)
	  // 3) Half-byte sumprobs.  (Expected for the turn in the trunk of
	  //    the final heads-up system.)
	  // 4) Char sumprobs.  (Expected for the trunk streets of the final
	  //    system - except as noted above.)
	  // 5) Bit regrets

	  // Default
	  methods_[p][st] = ProbMethod::REGRET_MATCHING;
	  sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.d", dir, st, it, p);
	  if (FileExists(buf)) {
	    value_types_[p][st] = CFR_DOUBLE;
	  } else {
	    sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.i", dir, st, it, p);
	    if (FileExists(buf)) {
	      value_types_[p][st] = CFR_INT;
	    } else {
	      sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.h", dir, st, it, p);
	      if (FileExists(buf)) {
		value_types_[p][st] = CFR_HALF_BYTE;
		if (st != 2) {
		  fprintf(stderr,
			  "Only expected half-byte sumprobs on turn\n");
		  exit(-1);
		}
	      } else {
		sprintf(buf, "%s/sumprobs.x.0.0.%u.%u.p%u.c", dir, st, it, p);
		if (FileExists(buf)) {
		  value_types_[p][st] = CFR_CHAR;
		} else {
		  sprintf(buf, "%s/regrets.x.0.0.%u.%u.p%u.b", dir, st, it, p);
		  if (FileExists(buf)) {
		    value_types_[p][st] = CFR_BITS;
		  } else {
		    sprintf(buf, "%s/regrets.x.0.0.%u.%u.p%u.c", dir, st, it,
			    p);
		    if (FileExists(buf)) {
		      value_types_[p][st] = CFR_CHAR;
		      methods_[p][st] = ProbMethod::PURE;
		    } else {
		      fprintf(stderr, "Couldn't find file %s p %u st %u "
			      "it %u\n", buf, p, st, it);
		      exit(-1);
		    }
		  }
		}
	      }
	    }
	  }
	  
	  readers_[p][st] = new Reader(buf);
	} else {
	  readers_[p][st] = nullptr;
	}
      }
    } else {
      readers_[p] = nullptr;
    }
  }

  offsets_ = new unsigned long long int **[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    if (players == nullptr || players[p]) {
      offsets_[p] = new unsigned long long int *[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	if (streets == nullptr || streets[st]) {
	  unsigned int num_nt = betting_tree->NumNonterminals(p, st);
	  offsets_[p][st] = new unsigned long long int[num_nt];
	  for (unsigned int i = 0; i < num_nt; ++i) {
	    offsets_[p][st][i] = kMaxUInt;
	  }
	} else {
	  offsets_[p][st] = nullptr;
	}
      }
    } else {
      offsets_[p] = nullptr;
    }
  }

  unsigned long long int **current = new unsigned long long int *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    current[p] = new unsigned long long int[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      current[p][st] = 0ULL;
    }
  }
  bool ***seen = new bool **[max_street + 1];
  for (unsigned int st = 0; st <= max_street; ++st) {
    seen[st] = new bool *[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      unsigned int num_nt = betting_tree->NumNonterminals(p, st);
      seen[st][p] = new bool[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	seen[st][p][i] = false;
      }
    }
  }
  InitializeOffsets(betting_tree->Root(), current, seen);
  for (unsigned int st = 0; st <= max_street; ++st) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete [] seen[st][p];
    }
    delete [] seen[st];
  }
  delete [] seen;
  for (unsigned int p = 0; p < num_players; ++p) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (readers_[p] && readers_[p][st]) {
	unsigned long long int file_size = readers_[p][st]->FileSize();
	if (file_size != current[p][st]) {
	  fprintf(stderr, "file_size %llu current %llu p %u st %u\n",
		file_size, current[p][st], p, st);
	  fprintf(stderr, "num_nt %u\n",
		  betting_tree->NumNonterminals(p, st));
	  fprintf(stderr, "num_holdings %u\n", num_holdings_[st]);
	  fprintf(stderr, "cvt %i\n", value_types_[p][st]);
	  fprintf(stderr, "file %s\n", readers_[p][st]->Filename().c_str());
	  exit(-1);
	}
      }
    }
    delete [] current[p];
  }
  delete [] current;
}

CFRValuesFile::~CFRValuesFile(void) {
  unsigned int num_players = Game::NumPlayers();
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int p = 0; p < num_players; ++p) {
    if (readers_[p]) {
      for (unsigned int st = 0; st <= max_street; ++st) {
	delete readers_[p][st];
      }
      delete [] readers_[p];
    }
  }
  delete [] readers_;
  for (unsigned int p = 0; p < num_players; ++p) {
    if (offsets_[p]) {
      for (unsigned int st = 0; st <= max_street; ++st) {
	delete [] offsets_[p][st];
      }
      delete [] offsets_[p];
    }
  }
  delete [] offsets_;

  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] value_types_[p];
    delete [] methods_[p];
  }
  delete [] value_types_;
  delete [] methods_;
  
  delete [] num_holdings_;
}

// This is a little tricky for the streets where we quantize to a half-byte
// or 2 bits.  The amount of data we have may not be exactly a whole number
// of bytes.  So we need to round up to a whole number of bytes.
void CFRValuesFile::InitializeOffsets(Node *node,
				      unsigned long long int **current,
				      bool ***seen) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  // Check for reentrant tree
  if (seen[st][pa][nt]) return;
  seen[st][pa][nt] = true;
  if (offsets_[pa] && offsets_[pa][st] && num_succs > 1) {
    offsets_[pa][st][nt] = current[pa][st];
    unsigned int num_values = num_holdings_[st] * num_succs;
    if (value_types_[pa][st] == CFR_CHAR) {
      current[pa][st] += num_values;
    } else if (value_types_[pa][st] == CFR_HALF_BYTE) {
      if (num_values % 2 == 0) current[pa][st] += num_values / 2;
      else                     current[pa][st] += num_values / 2 + 1;
    } else if (value_types_[pa][st] == CFR_BITS) {
      if (num_holdings_[st] % 4 == 0) {
	current[pa][st] += num_holdings_[st] / 4;
      } else {
	current[pa][st] += num_holdings_[st] / 4 + 1;
      }
    } else if (value_types_[pa][st] == CFR_INT) {
      current[pa][st] += num_values * 4;
    } else if (value_types_[pa][st] == CFR_DOUBLE) {
      current[pa][st] += num_values * 8;
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    InitializeOffsets(node->IthSucc(s), current, seen);
  }
}

// h may be either a bucket (for an abstracted system) or an hcp index
// (for an unabstracted system).
// For an unabstracted system, num_prior_h is board * num_hole_card_pairs.
//
// There are three ways the hand offset may be computed:
// 1) Abstracted system:
//    h * num_succs * size-of-value
// 2) Unabstracted system, not CFR_BITS:
//    (num_prior_h + h) * num_succs * size-of-value
// 3) Unabstracted system, CFR_BITS:
//
// restructure writes out a round number of bytes at every nonterminal.
// For a bucketed street, hand_offset is b * num_succs
// For an unbucketed street, hand_offset is
// board * num_hole_card_pairs * num_succs + hcp * num_succs.
void CFRValuesFile::Probs(unsigned int p, unsigned int st, unsigned int nt,
			  unsigned int h, unsigned int num_succs,
			  unsigned int dsi, double *probs) const {
  if (num_succs == 1) {
    probs[0] = 1.0;
    return;
  }
  unsigned long long int offset = offsets_[p][st][nt];
  if (value_types_[p][st] == CFR_CHAR) {
    offset += h * num_succs;
  } else if (value_types_[p][st] == CFR_HALF_BYTE) {
    offset += (h * num_succs) / 2;
  } else if (value_types_[p][st] == CFR_BITS) {
    offset += h / 4;
  } else if (value_types_[p][st] == CFR_INT) {
    offset += (h * num_succs) * 4;
  } else if (value_types_[p][st] == CFR_DOUBLE) {
    offset += (h * num_succs) * 8;
  } else {
    fprintf(stderr, "Currently expect files to be of type char, half-byte or "
	    "bits: %i\n", (int)value_types_[p][st]);
    exit(-1);
  }
  if (offset >= (unsigned long long int)readers_[p][st]->FileSize()) {
    fprintf(stderr, "Offset too high?!?  p %u st %u nt %u offset %llu "
	    "base offset %llu h %u fs %lli\n", p, st, nt, offset,
	    offsets_[p][st][nt], h, readers_[p][st]->FileSize());
    fprintf(stderr, "File: %s\n", readers_[p][st]->Filename().c_str());
    exit(-1);
  }
  readers_[p][st]->SeekTo(offset);

  if (methods_[p][st] == ProbMethod::PURE) {
    if (value_types_[p][st] == CFR_CHAR) {
      // Signed or unsigned?  Can this be regrets?
      unsigned int s;
      for (s = 0; s < num_succs; ++s) {
	unsigned char uc = readers_[p][st]->ReadUnsignedCharOrDie();
	if (uc == 0) break;
      }
      if (s == num_succs) {
	fprintf(stderr, "No zero regret succ?!?\n");
	exit(-1);
      }
      for (unsigned int s1 = 0; s1 < num_succs; ++s1) {
	probs[s1] = s1 == s ? 1.0 : 0;
      }
    } else {
      fprintf(stderr, "Pure: Unexpected value type %i\n",
	      (int)value_types_[p][st]);
      exit(-1);
    }
  } else {
    if (value_types_[p][st] == CFR_CHAR) {
      unique_ptr<unsigned char []> c_values(new unsigned char[num_succs]);
      unsigned int sum = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	unsigned char c = readers_[p][st]->ReadUnsignedCharOrDie();
	sum += c;
	c_values[s] = c;
      }
      if (sum == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  probs[s] = s == dsi ? 1.0 : 0;
	}
      } else {
	double d_sum = sum;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  probs[s] = c_values[s] / d_sum;
	}
      }
    } else if (value_types_[p][st] == CFR_HALF_BYTE) {
      bool high = (h * num_succs % 2) == 0;
      unique_ptr<unsigned char []> c_values(new unsigned char[num_succs]);
      unsigned char c = readers_[p][st]->ReadUnsignedCharOrDie();
      for (unsigned int s = 0; s < num_succs; ++s) {
	if (high) {
	  // The high 4 bits
	  c_values[s] = c >> 4;
	} else {
	  // The low 4 bits
	  c_values[s] = c & 15;
	  if (s != num_succs - 1) {
	    c = readers_[p][st]->ReadUnsignedCharOrDie();
	  }
	}
	high = ! high;
      }
      for (unsigned int s = 0; s < num_succs; ++s) {
	probs[s] = ((double)c_values[s]) / 15.0;
      }
    } else if (value_types_[p][st] == CFR_BITS) {
      unsigned int shift;
      if (h % 4 == 0)      shift = 6;
      else if (h % 4 == 1) shift = 4;
      else if (h % 4 == 2) shift = 2;
      else                 shift = 0;
      unique_ptr<unsigned char []> c_values(new unsigned char[num_succs]);
      unsigned char c = readers_[p][st]->ReadUnsignedCharOrDie();
      unsigned int best_s = (c >> shift) & 3;
      for (unsigned int s = 0; s < num_succs; ++s) {
	probs[s] = (s == best_s ? 1.0 : 0);
      }
    } else if (value_types_[p][st] == CFR_INT) {
      // Signed or unsigned?  Can this be regrets?
      unique_ptr<unsigned int []> ui_values(new unsigned int[num_succs]);
      long long int sum = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	unsigned int uiv = readers_[p][st]->ReadUnsignedIntOrDie();
	sum += uiv;
	ui_values[s] = uiv;
      }
      if (sum == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  probs[s] = s == dsi ? 1.0 : 0;
	}
      } else {
	double d_sum = sum;
	for (unsigned int s = 0; s < num_succs; ++s) {
	  probs[s] = ui_values[s] / d_sum;
	}
      }
    } else if (value_types_[p][st] == CFR_DOUBLE) {
      unique_ptr<double []> d_values(new double[num_succs]);
      double sum = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	double dv = readers_[p][st]->ReadDoubleOrDie();
	sum += dv;
	d_values[s] = dv;
      }
      if (sum == 0) {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  probs[s] = s == dsi ? 1.0 : 0;
	}
      } else {
	for (unsigned int s = 0; s < num_succs; ++s) {
	  probs[s] = d_values[s] / sum;
	}
      }
    } else {
      fprintf(stderr, "Unexpected value type %i\n", (int)value_types_[p][st]);
      exit(-1);
    }
  }
}

void CFRValuesFile::ReadPureSubtree(Node *whole_node, Node *subtree_node,
				    CFRValues *regrets) {
  if (whole_node->Terminal()) return;
  unsigned int num_succs = whole_node->NumSuccs();
  if (num_succs > 1) {
    unsigned int pa = whole_node->PlayerActing();
    unsigned int st = whole_node->Street();
    unsigned int whole_nt = whole_node->NonterminalID();
    unsigned int subtree_nt = subtree_node->NonterminalID();
    unsigned char *c_values;
    regrets->Values(pa, st, subtree_nt, &c_values);
    unsigned long long int offset = offsets_[pa][st][whole_nt];
    Reader *reader = readers_[pa][st];
    reader->SeekTo(offset);
    unsigned int num_buckets = num_holdings_[st];
    unsigned char c = reader->ReadUnsignedCharOrDie();
    unsigned int shift = 6;
    for (unsigned int b = 0; b < num_buckets; ++b) {
      unsigned int best_s = (c >> shift) & 3;
      unsigned char *this_regrets = c_values + b * num_succs;
      for (unsigned int s = 0; s < num_succs; ++s) {
	this_regrets[s] = s == best_s ? 1 : 0;
      }
      if (shift == 0 && b != num_buckets - 1) {
	c = reader->ReadUnsignedCharOrDie();
	shift = 6;
      } else {
	shift -= 2;
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    ReadPureSubtree(whole_node->IthSucc(s), subtree_node->IthSucc(s), regrets);
  }
}

void CFRValuesFile::ReadPureSubtree(Node *whole_node, BettingTree *subtree,
				    CFRValues *regrets) {
  regrets->AllocateAndClearChars(subtree->Root(), kMaxUInt);
  ReadPureSubtree(whole_node, subtree->Root(), regrets);
}
