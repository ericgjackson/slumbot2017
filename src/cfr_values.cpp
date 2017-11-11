// Should the tree (subtree) be a member?  Or the root node?

#include <stdio.h>
#include <stdlib.h>

#include "betting_tree.h"
#include "board_tree.h"
#include "buckets.h"
#include "card_abstraction.h"
#include "cfr_values.h"
#include "compression_utils.h"
#ifdef EJC
#include "ej_compress.h"
#else
#include "compressor.h"
#endif
#include "game.h"
#include "io.h"
#include "nonterminal_ids.h"

CFRValues::CFRValues(const bool *players, bool sumprobs, bool *streets,
		     const BettingTree *betting_tree, unsigned int root_bd,
		     unsigned int root_bd_st,
		     const CardAbstraction &card_abstraction,
		     const Buckets &buckets, const bool *compressed_streets) {
  unsigned int num_players = Game::NumPlayers();
  players_.reset(new bool[num_players]);
  if (players == nullptr) {
    for (unsigned int p = 0; p < num_players; ++p) {
      players_[p] = true;
    }
  } else {
    for (unsigned int p = 0; p < num_players; ++p) {
      players_[p] = players[p];
    }
  }
  sumprobs_ = sumprobs;
  unsigned int max_street = Game::MaxStreet();

  compressed_streets_.reset(new bool[max_street + 1]);
  if (compressed_streets == nullptr) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      compressed_streets_[st] = false;
    }
  } else {
    for (unsigned int st = 0; st <= max_street; ++st) {
      compressed_streets_[st] = compressed_streets[st];
    }
  }

  streets_.reset(new bool[max_street + 1]);
  if (streets == nullptr) {
    for (unsigned int st = 0; st <= max_street; ++st) {
      streets_[st] = true;
    }
  } else {
    for (unsigned int st = 0; st <= max_street; ++st) {
      streets_[st] = streets[st];
    }
  }

  num_nonterminals_ = new unsigned int *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    num_nonterminals_[p] = new unsigned int[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      num_nonterminals_[p][st] = betting_tree->NumNonterminals(p, st);
    }    
  }
  
  root_bd_ = root_bd;
  root_bd_st_ = root_bd_st;


  bucket_thresholds_.reset(new unsigned int[max_street + 1]);
  for (unsigned int st = 0; st <= max_street; ++st) {
    bucket_thresholds_[st] = card_abstraction.BucketThreshold(st);
  }

  num_card_holdings_ = new unsigned int *[num_players];
  num_bucket_holdings_ = new unsigned int *[num_players];
  for (unsigned int p = 0; p < num_players; ++p) {
    num_card_holdings_[p] = new unsigned int[max_street + 1];
    num_bucket_holdings_[p] = new unsigned int[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! streets_[st]) continue;
      unsigned int num_local_boards =
	BoardTree::NumLocalBoards(root_bd_st_, root_bd_, st);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      num_card_holdings_[p][st] = num_local_boards * num_hole_card_pairs;
      if (buckets.None(st)) {
	num_bucket_holdings_[p][st] = 0;
      } else {
	num_bucket_holdings_[p][st] = buckets.NumBuckets(st);
      }
    }
  }

#ifdef EJC
  new_distributions_ = new long long int *[max_street + 1];
#else
  new_distributions_ = new int64_t *[max_street + 1];
#endif
  for (unsigned int st = 0; st <= max_street; ++st) {
    if (! streets_[st] || ! compressed_streets_[st]) {
      new_distributions_[st] = nullptr;
      continue;
    }
#ifdef EJC
    new_distributions_[st] =
      new long long int[COMPRESSOR_DISTRIBUTION_SIZE];
#else
    new_distributions_[st] =
      new int64_t[COMPRESSOR_DISTRIBUTION_SIZE];
#endif
    for (unsigned int i = 0; i < COMPRESSOR_DISTRIBUTION_SIZE; ++i) {
      new_distributions_[st][i] = g_defaultDistribution[i];
    }
  }
  
  c_values_ = nullptr;
  s_values_ = nullptr;
  i_values_ = nullptr;
  d_values_ = nullptr;
}

CFRValues::~CFRValues(void) {
  unsigned int max_street = Game::MaxStreet();

  for (unsigned int st = 0; st <= max_street; ++st) {
    delete [] new_distributions_[st];
  }
  delete [] new_distributions_;

  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    if (! players_[p]) continue;
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! streets_[st]) continue;
      if (c_values_ && c_values_[p] && c_values_[p][st]) {
	unsigned int num_nt = num_nonterminals_[p][st];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  delete [] c_values_[p][st][i];
	}
	delete [] c_values_[p][st];
      }
      if (s_values_ && s_values_[p] && s_values_[p][st]) {
	unsigned int num_nt = num_nonterminals_[p][st];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  delete [] s_values_[p][st][i];
	}
	delete [] s_values_[p][st];
      }
      if (i_values_ && i_values_[p] && i_values_[p][st]) {
	unsigned int num_nt = num_nonterminals_[p][st];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  delete [] i_values_[p][st][i];
	}
	delete [] i_values_[p][st];
      }
      if (d_values_ && d_values_[p] && d_values_[p][st]) {
	unsigned int num_nt = num_nonterminals_[p][st];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  delete [] d_values_[p][st][i];
	}
	delete [] d_values_[p][st];
      }
    }
    if (c_values_) delete [] c_values_[p];
    if (s_values_) delete [] s_values_[p];
    if (i_values_) delete [] i_values_[p];
    if (d_values_) delete [] d_values_[p];
  }
  delete [] c_values_;
  delete [] s_values_;
  delete [] i_values_;
  delete [] d_values_;

  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] num_card_holdings_[p];
    delete [] num_bucket_holdings_[p];
  }
  delete [] num_card_holdings_;
  delete [] num_bucket_holdings_;

  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] num_nonterminals_[p];
  }
  delete [] num_nonterminals_;
}

// Zeroes out values too
void CFRValues::AllocateAndClear(Node *node, CFRValueType value_type,
				 unsigned int only_p) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int st = node->Street();
  unsigned int p = node->PlayerActing();
  if (streets_[st] && players_[p] && (only_p == kMaxUInt || p == only_p) &&
      num_succs > 1) {
    unsigned int nt = node->NonterminalID();
    // Check for reentrant nodes
    if (! (value_type == CFR_CHAR && c_values_[p][st][nt]) &&
	! (value_type == CFR_SHORT && s_values_[p][st][nt]) &&
	! (value_type == CFR_INT && i_values_[p][st][nt]) &&
	! (value_type == CFR_DOUBLE && d_values_[p][st][nt])) {
      bool bucketed = num_bucket_holdings_[p][st] > 0 &&
	node->LastBetTo() < bucket_thresholds_[st];
      unsigned int num_holdings;
      if (bucketed) {
	num_holdings = num_bucket_holdings_[p][st];
      } else {
	num_holdings = num_card_holdings_[p][st];
      }
      unsigned int num_actions = num_holdings * num_succs;
      if (value_type == CFR_CHAR) {
	unsigned char *vals = new unsigned char[num_actions];
	for (unsigned int a = 0; a < num_actions; ++a) vals[a] = 0;
	c_values_[p][st][nt] = vals;
      } else if (value_type == CFR_SHORT) {
	unsigned short *vals = new unsigned short[num_actions];
	for (unsigned int a = 0; a < num_actions; ++a) vals[a] = 0;
	s_values_[p][st][nt] = vals;
      } else if (value_type == CFR_INT) {
	int *vals = new int[num_actions];
	for (unsigned int a = 0; a < num_actions; ++a) vals[a] = 0;
	i_values_[p][st][nt] = vals;
      } else {
	double *vals = new double[num_actions];
	for (unsigned int a = 0; a < num_actions; ++a) vals[a] = 0;
	d_values_[p][st][nt] = vals;
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    AllocateAndClear(node->IthSucc(s), value_type, only_p);
  }
}

void CFRValues::AllocateAndClearChars(Node *node, unsigned int only_p) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  if (c_values_ == nullptr) {
    c_values_ = new unsigned char ***[num_players];
    for (unsigned int p = 0; p < num_players; ++p) c_values_[p] = nullptr;
  }
  for (unsigned int p = 0; p < num_players; ++p) {
    // Skip player if a) players_[p] is false, or b) only_p is set to a
    // different player.
    if (! players_[p] || (only_p != kMaxUInt && p != only_p)) {
      continue;
    }
    c_values_[p] = new unsigned char **[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (streets_[st]) {
	unsigned int num_nt = num_nonterminals_[p][st];
	c_values_[p][st] = new unsigned char *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  c_values_[p][st][i] = nullptr;
	}
      } else {
	c_values_[p][st] = nullptr;
      }
    }
  }

  AllocateAndClear(node, CFR_CHAR, only_p);
}

void CFRValues::AllocateAndClearShorts(Node *node, unsigned int only_p) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  if (s_values_ == nullptr) {
    s_values_ = new unsigned short ***[num_players];
    for (unsigned int p = 0; p < num_players; ++p) s_values_[p] = nullptr;
  }
  for (unsigned int p = 0; p < num_players; ++p) {
    // Skip player if a) players_[p] is false, or b) only_p is set to a
    // different player.
    if (! players_[p] || (only_p != kMaxUInt && p != only_p)) {
      continue;
    }
    s_values_[p] = new unsigned short **[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (streets_[st]) {
	unsigned int num_nt = num_nonterminals_[p][st];
	s_values_[p][st] = new unsigned short *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  s_values_[p][st][i] = nullptr;
	}
      } else {
	s_values_[p][st] = nullptr;
      }
    }
  }

  AllocateAndClear(node, CFR_SHORT, only_p);
}

void CFRValues::AllocateAndClearInts(Node *node, unsigned int only_p) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  if (i_values_ == nullptr) {
    i_values_ = new int ***[num_players];
    for (unsigned int p = 0; p < num_players; ++p) i_values_[p] = nullptr;
  }
  for (unsigned int p = 0; p < num_players; ++p) {
    // Skip player if a) players_[p] is false, or b) only_p is set to a
    // different player.
    if (! players_[p] || (only_p != kMaxUInt && p != only_p)) {
      continue;
    }
    i_values_[p] = new int **[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (streets_[st]) {
	unsigned int num_nt = num_nonterminals_[p][st];
	i_values_[p][st] = new int *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  i_values_[p][st][i] = nullptr;
	}
      } else {
	i_values_[p][st] = nullptr;
      }
    }
  }

  AllocateAndClear(node, CFR_INT, only_p);
}

void CFRValues::AllocateAndClearDoubles(Node *node, unsigned int only_p) {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  if (d_values_ == nullptr) {
    d_values_ = new double ***[num_players];
    for (unsigned int p = 0; p < num_players; ++p) d_values_[p] = nullptr;
  }
  for (unsigned int p = 0; p < num_players; ++p) {
    // Skip player if a) players_[p] is false, or b) only_p is set to a
    // different player.
    if (! players_[p] || (only_p != kMaxUInt && p != only_p)) {
      continue;
    }
    d_values_[p] = new double **[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (streets_[st]) {
	if (compressed_streets_[st]) {
	  fprintf(stderr, "Can't use compression in combination with "
		  "doubles\n");
	  exit(-1);
	}
	unsigned int num_nt = num_nonterminals_[p][st];
	d_values_[p][st] = new double *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  d_values_[p][st][i] = nullptr;
	}
      } else {
	d_values_[p][st] = nullptr;
      }
    }
  }

  AllocateAndClear(node, CFR_DOUBLE, only_p);
}

// We delete the inner arrays, but never any of the outer arrays.
// They will get deleted eventually in CFRValues::~CFRValues().
void CFRValues::DeleteBelow(Node *node) {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int st = node->Street();
  unsigned int p = node->PlayerActing();
  if (streets_[st] && players_[p]) {
    unsigned int nt = node->NonterminalID();
    if (c_values_ && c_values_[p] && c_values_[p][st]) {
      delete [] c_values_[p][st][nt];
      c_values_[p][st][nt] = nullptr;
    } else if (s_values_ && s_values_[p] && s_values_[p][st]) {
      delete [] s_values_[p][st][nt];
      s_values_[p][st][nt] = nullptr;
    } else if (i_values_ && i_values_[p] && i_values_[p][st]) {
      delete [] i_values_[p][st][nt];
      i_values_[p][st][nt] = nullptr;
    } else if (d_values_&& d_values_[p] && d_values_[p][st]) {
      delete [] d_values_[p][st][nt];
      d_values_[p][st][nt] = nullptr;
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    DeleteBelow(node->IthSucc(s));
  }
}

void CFRValues::WriteNode(Node *node, Writer *writer,
			  void *compressor, unsigned int num_holdings,
			  unsigned int offset) const {
  unsigned int p = node->PlayerActing();
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  unsigned int num_succs = node->NumSuccs();
  if (num_succs <= 1) return;
  if (compressed_streets_[st]) {
    // This code doesn't support num_holdings passed in
    unsigned int num_local_boards =
      BoardTree::NumLocalBoards(root_bd_st_, root_bd_, st);
    unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
    unsigned int num_bd_actions = num_hole_card_pairs * num_succs;
    for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
#ifdef EJC
      EJCompressor *compressor = (EJCompressor *)compressor;
      compressor->Compress((int *)&i_values_[p][st][nt][lbd * num_bd_actions +
							offset],
			   lbd ?
			   (int *)&i_values_[p][st][nt][(lbd - 1) *
							num_bd_actions +
							offset] : NULL,
			   num_bd_actions, num_succs);
#else
      Compress(compressor,
	       (int *)&i_values_[p][st][nt][lbd * num_bd_actions + offset],
	       lbd ?
	       (int *)&i_values_[p][st][nt][(lbd - 1) * num_bd_actions +
					    offset] :
	       NULL, num_bd_actions, num_succs);
#endif
    }
  } else {
    unsigned int num_actions = num_holdings * num_succs;
    for (unsigned int a = 0; a < num_actions; ++a) {
#if 0
	// Used to have this code for when we want to only write out a
	// subpart of the strategy that we computed corresponding to a
	// target board.
      if (target_bd != kMaxUInt) {
	unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
	if (st > target_st) {
	  fprintf(stderr, "Not supported yet\n");
	  exit(-1);
	} else if (st < target_st) {
	  fprintf(stderr, "Shouldn't happen\n");
	  exit(-1);
	} else {
	  unsigned int lbd = a / (num_hole_card_pairs * num_succs);
	  unsigned int gbd = BoardTree::GlobalIndex(root_bd_st_, root_bd_,
						    st, lbd);
	  if (gbd != target_bd) continue;
	}
      }
#endif
      if (c_values_ && c_values_[p] && c_values_[p][st]) {
	writer->WriteUnsignedChar(c_values_[p][st][nt][a + offset]);
      } else if (s_values_ && s_values_[p] && s_values_[p][st]) {
	writer->WriteUnsignedShort(s_values_[p][st][nt][a + offset]);
      } else if (i_values_ && i_values_[p] && i_values_[p][st]) {
	writer->WriteInt(i_values_[p][st][nt][a + offset]);
      } else if (d_values_&& d_values_[p] && d_values_[p][st]) {
	writer->WriteDouble(d_values_[p][st][nt][a + offset]);
      }
    }
  }
}

// Prevent redunant writing with reentrant trees
void CFRValues::Write(Node *node, Writer ***writers,
		      void ***compressors, bool ***seen) const {
  if (node->Terminal()) return;
  unsigned int num_succs = node->NumSuccs();
  unsigned int st = node->Street();
  unsigned int pa = node->PlayerActing();
  if (streets_[st] && players_[pa] && writers[pa]) {
    unsigned int nt = node->NonterminalID();
    // If we have seen this node, we have also seen all the descendants of it,
    // so we can just return.
    if (seen) {
      if (seen[st][pa][nt]) return;
      seen[st][pa][nt] = true;
    }
    bool bucketed = num_bucket_holdings_[pa][st] > 0 &&
      node->LastBetTo() < bucket_thresholds_[st];
    unsigned int num_holdings;
    if (bucketed) {
      num_holdings = num_bucket_holdings_[pa][st];
    } else {
      num_holdings = num_card_holdings_[pa][st];
    }
    WriteNode(node, writers[pa][st],
	      compressors ? compressors[pa][st] : nullptr, num_holdings, 0);
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    Write(node->IthSucc(s), writers, compressors, seen);
  }
}

void CFRValues::DeleteWriters(Writer ***writers, void ***compressors) const {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  for (unsigned int p = 0; p < num_players; ++p) {
    if (writers[p] == nullptr) continue;
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (compressors[p][st]) {
#ifdef EJC
	delete (EJCompressor *)compressors[p][st];
#else
	DeleteCompressor(compressors[p][st]);
#endif
      }
      delete writers[p][st];
    }
    delete [] writers[p];
    delete [] compressors[p];
  }
  delete [] writers;
  delete [] compressors;
}

Writer ***CFRValues::InitializeWriters(const char *dir, unsigned int it,
				       const string &action_sequence,
				       unsigned int only_p,
				       void ****compressors) const {
  char buf[500];
  Mkdir(dir);
  unsigned int num_players = Game::NumPlayers();
  Writer ***writers = new Writer **[num_players];
  *compressors = new void **[num_players];
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int p = 0; p < num_players; ++p) {
    if (only_p != kMaxUInt && p != only_p) {
      writers[p] = nullptr;
      (*compressors)[p] = nullptr;
      continue;
    }
    if (! players_[p]) {
      writers[p] = nullptr;
      (*compressors)[p] = nullptr;
      continue;
    }
    writers[p] = new Writer *[max_street + 1];
    (*compressors)[p] = new void *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! streets_[st]) {
	writers[p][st] = nullptr;
	(*compressors)[p][st] = nullptr;
	continue;
      }
      const char *suffix = i_values_ && i_values_[p] && i_values_[p][st] ?
	"i" : "d";
      sprintf(buf, "%s/%s.%s.%u.%u.%u.%u.p%u.%s", dir,
	      sumprobs_ ? "sumprobs" : "regrets", action_sequence.c_str(),
	      root_bd_st_, root_bd_, st, it, p, suffix);
      writers[p][st] = new Writer(buf);
      if (compressed_streets_[st]) {
#ifdef EJC
	(*compressors)[p][st] =
	  (void *)new EJCompressor(new_distributions_[st],
				   new_distributions_[st],
				   EJCompressCallback, writers[p][st]);
#else
	(*compressors)[p][st] = CreateCompressor(NORMAL_COMPRESSOR, 
						 new_distributions_[st],
						 new_distributions_[st],
						 CompressCallback,
						 writers[p][st], nullptr);
#endif
      } else {
	(*compressors)[p][st] = nullptr;
      }
    }
  }
  return writers;
}

// Some care is required if we are writing the values for a subtree.  The
// "root" node passed in should be from the tree that was used to initialize
// the CFRValues object.  We need the nonterminal IDs to match.  However, the
// subtree_nt passed in should be the nonterminal ID of the root of the subtree
// in the entire tree.  This nonterminal ID forms part of the filename.  When
// we read the subtree strategy in, in order to incorporate it into a full
// tree strategy, we will use this "full tree" nonterminal ID.
void CFRValues::Write(const char *dir, unsigned int it, Node *root,
		      const string &action_sequence,
		      unsigned int only_p) const {
  unsigned int max_street = Game::MaxStreet();
  unsigned int num_players = Game::NumPlayers();
  bool ***seen = new bool **[max_street + 1];
  unsigned int root_st = root->Street();
  unsigned int **num_nonterminals = CountNumNonterminals(root);
  for (unsigned int st = root_st; st <= max_street; ++st) {
    seen[st] = new bool *[num_players];
    for (unsigned int p = 0; p < num_players; ++p) {
      unsigned int num_nt = num_nonterminals[p][st];
      seen[st][p] = new bool[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	seen[st][p][i] = false;
      }
    }
  }
  void ***compressors;
  Writer ***writers = InitializeWriters(dir, it, action_sequence,
					only_p, &compressors);
  Write(root, writers, compressors, seen);
  DeleteWriters(writers, compressors);
  for (unsigned int st = root_st; st <= max_street; ++st) {
    for (unsigned int p = 0; p < num_players; ++p) {
      delete [] seen[st][p];
    }
    delete [] seen[st];
  }
  delete [] seen;
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] num_nonterminals[p];
  }
  delete [] num_nonterminals;
}

Reader *CFRValues::InitializeReader(const char *dir, unsigned int p,
				    unsigned int st, unsigned int it,
				    const string &action_sequence,
				    unsigned int root_bd_st,
				    unsigned int root_bd,
				    CFRValueType *value_type) {
  char buf[500];
  
  sprintf(buf, "%s/%s.%s.%u.%u.%u.%u.p%u.d", dir,
	  sumprobs_ ? "sumprobs" : "regrets", action_sequence.c_str(),
	  root_bd_st, root_bd, st, it, p);
  if (FileExists(buf)) {
    *value_type = CFR_DOUBLE;
  } else {
    sprintf(buf, "%s/%s.%s.%u.%u.%u.%u.p%u.i", dir,
	    sumprobs_ ? "sumprobs" : "regrets", action_sequence.c_str(),
	    root_bd_st, root_bd, st, it, p);
    if (FileExists(buf)) {
      *value_type = CFR_INT;
    } else {
      sprintf(buf, "%s/%s.%s.%u.%u.%u.%u.p%u.c", dir,
	      sumprobs_ ? "sumprobs" : "regrets", action_sequence.c_str(),
	      root_bd_st, root_bd, st, it, p);
      if (FileExists(buf)) {
	*value_type = CFR_CHAR;
      } else {
	sprintf(buf, "%s/%s.%s.%u.%u.%u.%u.p%u.s", dir,
		sumprobs_ ? "sumprobs" : "regrets", action_sequence.c_str(),
		root_bd_st, root_bd, st, it, p);
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
  if (compressed_streets_[st] && *value_type != CFR_INT) {
    fprintf(stderr, "Can only use compression in combination with ints\n");
    exit(-1);
  }
#if 0
  // Can't do this test any more because this betting round may contain a
  // mixture of bucketed nodes and card nodes.
  // See if file size matches what we expect.  Only works if file is not
  // compressed.
  unsigned long long int file_size = FileSize(buf);
  unsigned int file_nh;
  if (*int_type) {
    file_nh = file_size / (sizeof(int) * num_street_actions);
  } else {
    file_nh = file_size / (sizeof(double) * num_street_actions);
  }
  if (! compressed_streets_[st]) {
    bool bucketed = num_bucket_holdings_[p][st] > 0 &&
      node->LastBetTo() < bucket_thresholds_[st];
    unsigned int num_holdings;
    if (bucketed) {
      num_holdings = num_bucket_holdings_[p][st];
    } else {
      num_holdings = num_card_holdings_[p][st];
    }
    if (file_nh != num_holdings) {
      fprintf(stderr, "Unexpected file size st %u: %llu\n", st, file_size);
      fprintf(stderr, "Num holdings: %u\n", num_holdings);
      fprintf(stderr, "%s\n", *int_type ? "INTS" : "DOUBLES");
      fprintf(stderr, "Expected file size: %llu\n",
	      ((unsigned long long int)num_holdings) *
	      ((unsigned long long int)
	       (*int_type ? sizeof(int) : sizeof(double))) *
	      num_street_actions);
      exit(-1);
    }
  }
#endif
  Reader *reader = new Reader(buf);
  return reader;
}

void CFRValues::InitializeValuesForReading(unsigned int p, unsigned int st,
					   unsigned int nt, Node *node,
					   CFRValueType value_type) {
  unsigned int num_succs = node->NumSuccs();
  if (num_succs <= 1) return;
  bool bucketed = num_bucket_holdings_[p][st] > 0 &&
    node->LastBetTo() < bucket_thresholds_[st];
  unsigned int num_holdings;
  if (bucketed) {
    num_holdings = num_bucket_holdings_[p][st];
  } else {
    num_holdings = num_card_holdings_[p][st];
  }
  unsigned int num_actions = num_holdings * num_succs;
  if (value_type == CFR_CHAR) {
    if (c_values_ == nullptr) {
      unsigned int num_players = Game::NumPlayers();
      c_values_ = new unsigned char ***[num_players];
      for (unsigned int i = 0; i < num_players; ++i) c_values_[i] = nullptr;
    }
    if (c_values_[p] == nullptr) {
      unsigned int max_street = Game::MaxStreet();
      c_values_[p] = new unsigned char **[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	c_values_[p][st] = nullptr;
      }
    }
    if (c_values_[p][st] == nullptr) {
      unsigned int num_nt = num_nonterminals_[p][st];
      c_values_[p][st] = new unsigned char *[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	c_values_[p][st][i] = nullptr;
      }
    }
    if (c_values_[p][st][nt] == nullptr) {
      c_values_[p][st][nt] = new unsigned char[num_actions];
    }
  } else if (value_type == CFR_SHORT) {
    if (s_values_ == nullptr) {
      unsigned int num_players = Game::NumPlayers();
      s_values_ = new unsigned short ***[num_players];
      for (unsigned int i = 0; i < num_players; ++i) s_values_[i] = nullptr;
    }
    if (s_values_[p] == nullptr) {
      unsigned int max_street = Game::MaxStreet();
      s_values_[p] = new unsigned short **[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	s_values_[p][st] = nullptr;
      }
    }
    if (s_values_[p][st] == nullptr) {
      unsigned int num_nt = num_nonterminals_[p][st];
      s_values_[p][st] = new unsigned short *[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	s_values_[p][st][i] = nullptr;
      }
    }
    if (s_values_[p][st][nt] == nullptr) {
      s_values_[p][st][nt] = new unsigned short[num_actions];
    }
  } else if (value_type == CFR_INT) {
    if (i_values_ == nullptr) {
      unsigned int num_players = Game::NumPlayers();
      i_values_ = new int ***[num_players];
      for (unsigned int i = 0; i < num_players; ++i) i_values_[i] = nullptr;
    }
    if (i_values_[p] == nullptr) {
      unsigned int max_street = Game::MaxStreet();
      i_values_[p] = new int **[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	i_values_[p][st] = nullptr;
      }
    }
    if (i_values_[p][st] == nullptr) {
      unsigned int num_nt = num_nonterminals_[p][st];
      i_values_[p][st] = new int *[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	i_values_[p][st][i] = nullptr;
      }
    }
    if (i_values_[p][st][nt] == nullptr) {
      i_values_[p][st][nt] = new int[num_actions];
    }
  } else {
    if (d_values_ == nullptr) {
      unsigned int num_players = Game::NumPlayers();
      d_values_ = new double ***[num_players];
      for (unsigned int i = 0; i < num_players; ++i) d_values_[i] = nullptr;
    }
    if (d_values_[p] == nullptr) {
      unsigned int max_street = Game::MaxStreet();
      d_values_[p] = new double **[max_street + 1];
      for (unsigned int st = 0; st <= max_street; ++st) {
	d_values_[p][st] = nullptr;
      }
    }
    if (d_values_[p][st] == nullptr) {
      unsigned int num_nt = num_nonterminals_[p][st];
      d_values_[p][st] = new double *[num_nt];
      for (unsigned int i = 0; i < num_nt; ++i) {
	d_values_[p][st][i] = nullptr;
      }
    }
    if (d_values_[p][st][nt] == nullptr) {
      d_values_[p][st][nt] = new double[num_actions];
    }
  }
}

void CFRValues::ReadNode(Node *node, Reader *reader, void *decompressor,
			 unsigned int num_holdings, CFRValueType value_type,
			 unsigned int offset) {
  unsigned int num_succs = node->NumSuccs();
  if (num_succs <= 1) return;
  unsigned int st = node->Street();
  unsigned int p = node->PlayerActing();
  unsigned int nt = node->NonterminalID();
  // Assume this is because this node is reentrant.
  // Does this do the right thing when called from endgame_utils.cpp?
  if ((value_type == CFR_CHAR && c_values_ && c_values_[p] &&
       c_values_[p][st] && c_values_[p][st][nt]) ||
      (value_type == CFR_SHORT && s_values_ && s_values_[p] &&
       s_values_[p][st] && s_values_[p][st][nt]) ||
      (value_type == CFR_INT && i_values_ && i_values_[p] &&
       i_values_[p][st] && i_values_[p][st][nt]) ||
      (value_type == CFR_DOUBLE && d_values_ && d_values_[p] &&
       d_values_[p][st] && d_values_[p][st][nt])) {
    return;
  }
  InitializeValuesForReading(p, st, nt, node, value_type);
  unsigned int num_actions = num_holdings * num_succs;
  if (value_type == CFR_CHAR) {
    for (unsigned int a = 0; a < num_actions; ++a) {
      c_values_[p][st][nt][offset + a] = reader->ReadUnsignedCharOrDie();
    }
  } else if (value_type == CFR_SHORT) {
    for (unsigned int a = 0; a < num_actions; ++a) {
      s_values_[p][st][nt][offset + a] = reader->ReadUnsignedShortOrDie();
    }
  } else if (value_type == CFR_INT) {
    if (compressed_streets_[st]) {
#ifdef EJC
      EJDecompressor *decompressor = (EJDecompressor *)decompressor;
#endif
      unsigned int num_local_boards =
	BoardTree::NumLocalBoards(root_bd_st_, root_bd_, st);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int num_bd_actions = num_hole_card_pairs * num_succs;
      for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
#ifdef EJC
	decompressor->Decompress(&i_values_[p][st][nt][lbd * num_bd_actions],
				 lbd ?
				 &i_values_[p][st][nt][(lbd-1) *
						       num_bd_actions +
						       offset] : NULL,
				 num_bd_actions, num_succs);
#else
	Decompress(decompressor, &i_values_[p][st][nt][lbd *
						       num_bd_actions + offset],
		   lbd ? &i_values_[p][st][nt][(lbd-1) * num_bd_actions +
					       offset] : NULL,
		   num_bd_actions, num_succs);
#endif
      }
    } else {
      for (unsigned int a = 0; a < num_actions; ++a) {
	i_values_[p][st][nt][offset + a] = reader->ReadIntOrDie();
      }
    }
  } else {
    for (unsigned int a = 0; a < num_actions; ++a) {
      d_values_[p][st][nt][offset + a] = reader->ReadDoubleOrDie();
    }
  }
}

void CFRValues::Read(Node *node, Reader ***readers, void ***decompressors,
		     CFRValueType **value_types, unsigned int only_p) {
  if (node->Terminal()) return;
  unsigned int st = node->Street();
  unsigned int num_succs = node->NumSuccs();
  unsigned int p = node->PlayerActing();
  if (streets_[st] && players_[p] && ! (only_p <= 1 && p != only_p)) {
    Reader *reader = readers[p][st];
    if (reader == nullptr) {
      fprintf(stderr, "CFRValues::Read(): p %u st %u missing file?\n", p, st);
      exit(-1);
    }
    bool bucketed = num_bucket_holdings_[p][st] > 0 &&
      node->LastBetTo() < bucket_thresholds_[st];
    unsigned int num_holdings;
    if (bucketed) {
      num_holdings = num_bucket_holdings_[p][st];
    } else {
      num_holdings = num_card_holdings_[p][st];
    }
    ReadNode(node, reader, decompressors ? decompressors[p][st] : nullptr,
	     num_holdings, value_types[p][st], 0);

  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    Read(node->IthSucc(s), readers, decompressors, value_types, only_p);
  }
}

// Some care is required if we are reading the values for a subtree.
// The "root" node passed in should be from a tree just for the subtree,
// so we will get dense nonterminal IDs.  The subtree_nt passed in should
// be the nonterminal ID of the root of the subtree in the entire tree.
// Because that nonterminal ID forms part of the filename.
void CFRValues::Read(const char *dir, unsigned int it, Node *root,
		     const string &action_sequence, unsigned int only_p) {
  unsigned int num_players = Game::NumPlayers();
  Reader ***readers = new Reader **[num_players];
  void ***decompressors = new void **[num_players];
  CFRValueType **value_types = new CFRValueType *[num_players];
  unsigned int max_street = Game::MaxStreet();

  for (unsigned int p = 0; p < num_players; ++p) {
    if (only_p != kMaxUInt && p != only_p) {
      readers[p] = nullptr;
      value_types[p] = nullptr;
      decompressors[p] = nullptr;
      continue;
    }
    if (! players_[p]) {
      readers[p] = nullptr;
      value_types[p] = nullptr;
      decompressors[p] = nullptr;
      continue;
    }
    readers[p] = new Reader *[max_street + 1];
    value_types[p] = new CFRValueType[max_street + 1];
    decompressors[p] = new void *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! streets_[st]) {
	readers[p][st] = nullptr;
	decompressors[p][st] = nullptr;
	continue;
      }
      readers[p][st] = InitializeReader(dir, p, st, it, action_sequence,
					root_bd_st_, root_bd_,
					&value_types[p][st]);
      if (compressed_streets_[st]) {
#ifdef EJC
	  decompressors[p][st] = new EJDecompressor(EJDecompressCallback,
	    readers[p][st], NULL);
#else
	  decompressors[p][st] =
	    CreateDecompressor(DecompressCallback, readers[p][st], NULL);
#endif
      } else {
	decompressors[p][st] = nullptr;
      }
    }
  }

  Read(root, readers, decompressors, value_types, only_p);
  
  for (unsigned int p = 0; p < num_players; ++p) {
    if (only_p != kMaxUInt && p != only_p) {
      continue;
    }
    if (! players_[p]) {
      continue;
    }
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! streets_[st]) continue;
#ifdef EJC
      delete (EJDecompressor *)decompressors[p][st];
#else
      if (decompressors[p][st]) DeleteDecompressor(decompressors[p][st]);
#endif
      if (! readers[p][st]->AtEnd()) {
	fprintf(stderr, "Reader p %u st %u didn't get to end\n", p, st);
	fprintf(stderr, "Pos: %lli\n", readers[p][st]->BytePos());
	fprintf(stderr, "File size: %lli\n", readers[p][st]->FileSize());
	exit(-1);
      }
      delete readers[p][st];
    }
    delete [] readers[p];
    delete [] decompressors[p];
  }
  delete [] readers;
  delete [] decompressors;
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] value_types[p];
  }
  delete [] value_types;
}

// Handle buckets.
void CFRValues::MergeInto(Node *full_node, Node *subgame_node,
			  unsigned int root_bd_st,
			  unsigned int root_bd,
			  const CFRValues &subgame_values,
			  const Buckets &buckets, unsigned int final_st) {
  if (full_node->Terminal()) return;
  unsigned int st = full_node->Street();
  if (st > final_st) return;
  unsigned int num_succs = full_node->NumSuccs();
  unsigned int p = full_node->PlayerActing();
  if (players_[p] && subgame_values.players_[p] && num_succs > 1) {
    bool chars = subgame_values.Chars(p, st);
    bool shorts = subgame_values.Shorts(p, st);
    bool ints = subgame_values.Ints(p, st);
    if (chars) {
      if (c_values_ == nullptr) {
	unsigned int num_players = Game::NumPlayers();
	c_values_ = new unsigned char ***[num_players];
	for (unsigned int i = 0; i < num_players; ++i) c_values_[i] = nullptr;
      }
      if (c_values_[p] == nullptr) {
	unsigned int max_street = Game::MaxStreet();
	c_values_[p] = new unsigned char **[max_street + 1];
	for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
	  c_values_[p][st1] = nullptr;
	}
      }
      if (c_values_[p][st] == nullptr) {
	unsigned int num_nt = num_nonterminals_[p][st];
	c_values_[p][st] = new unsigned char *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  c_values_[p][st][i] = nullptr;
	}
      }
    } else if (shorts) {
      if (s_values_ == nullptr) {
	unsigned int num_players = Game::NumPlayers();
	s_values_ = new unsigned short ***[num_players];
	for (unsigned int i = 0; i < num_players; ++i) s_values_[i] = nullptr;
      }
      if (s_values_[p] == nullptr) {
	unsigned int max_street = Game::MaxStreet();
	s_values_[p] = new unsigned short **[max_street + 1];
	for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
	  s_values_[p][st1] = nullptr;
	}
      }
      if (s_values_[p][st] == nullptr) {
	unsigned int num_nt = num_nonterminals_[p][st];
	s_values_[p][st] = new unsigned short *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  s_values_[p][st][i] = nullptr;
	}
      }
    } else if (ints) {
      if (i_values_ == nullptr) {
	unsigned int num_players = Game::NumPlayers();
	i_values_ = new int ***[num_players];
	for (unsigned int i = 0; i < num_players; ++i) i_values_[i] = nullptr;
      }
      if (i_values_[p] == nullptr) {
	unsigned int max_street = Game::MaxStreet();
	i_values_[p] = new int **[max_street + 1];
	for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
	  i_values_[p][st1] = nullptr;
	}
      }
      if (i_values_[p][st] == nullptr) {
	unsigned int num_nt = num_nonterminals_[p][st];
	i_values_[p][st] = new int *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  i_values_[p][st][i] = nullptr;
	}
      }
    } else {
      if (d_values_ == nullptr) {
	unsigned int num_players = Game::NumPlayers();
	d_values_ = new double ***[num_players];
	for (unsigned int i = 0; i < num_players; ++i) d_values_[i] = nullptr;
      }
      if (d_values_[p] == nullptr) {
	unsigned int max_street = Game::MaxStreet();
	d_values_[p] = new double **[max_street + 1];
	for (unsigned int st1 = 0; st1 <= max_street; ++st1) {
	  d_values_[p][st1] = nullptr;
	}
      }
      if (d_values_[p][st] == nullptr) {
	unsigned int num_nt = num_nonterminals_[p][st];
	d_values_[p][st] = new double *[num_nt];
	for (unsigned int i = 0; i < num_nt; ++i) {
	  d_values_[p][st][i] = nullptr;
	}
      }
    }

    bool bucketed = num_bucket_holdings_[p][st] > 0 &&
      full_node->LastBetTo() < bucket_thresholds_[st];
    unsigned int num_holdings;
    if (bucketed) {
      num_holdings = num_bucket_holdings_[p][st];
    } else {
      num_holdings = num_card_holdings_[p][st];
    }
#if 0
    if (num_holdings == 0) {
      if (buckets.None(st)) {
	// Note that root_bd_st_ is the root street of the CFRValues object
	// being merged into.  It can be different from root_bd_st which is
	// the root street of the CFRValues object being merged.
	unsigned int num_local_boards =
	  BoardTree::NumLocalBoards(root_bd_st_, root_bd_, st);
	unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
	num_holdings = num_local_boards * num_hole_card_pairs;
      } else {
	num_holdings = buckets.NumBuckets(st);
      }
      num_holdings_[p][st] = num_holdings;
    }
#endif
    unsigned int num_actions = num_holdings * num_succs;
    
    unsigned int full_nt = full_node->NonterminalID();
    unsigned int subgame_nt = subgame_node->NonterminalID();
    if (chars) {
      if (c_values_[p][st][full_nt] == nullptr) {
	c_values_[p][st][full_nt] = new unsigned char[num_actions];
	// Zeroing out is needed for bucketed case
	for (unsigned int a = 0; a < num_actions; ++a) {
	  c_values_[p][st][full_nt][a] = 0;
	}
      }
    } else if (shorts) {
      if (s_values_[p][st][full_nt] == nullptr) {
	s_values_[p][st][full_nt] = new unsigned short[num_actions];
	// Zeroing out is needed for bucketed case
	for (unsigned int a = 0; a < num_actions; ++a) {
	  s_values_[p][st][full_nt][a] = 0;
	}
      }
    } else if (ints) {
      if (i_values_[p][st][full_nt] == nullptr) {
	i_values_[p][st][full_nt] = new int[num_actions];
	// Zeroing out is needed for bucketed case
	for (unsigned int a = 0; a < num_actions; ++a) {
	  i_values_[p][st][full_nt][a] = 0;
	}
      }
    } else {
      if (d_values_[p][st][full_nt] == nullptr) {
	d_values_[p][st][full_nt] = new double[num_actions];
	// Zeroing out is needed for bucketed case
	for (unsigned int a = 0; a < num_actions; ++a) {
	  d_values_[p][st][full_nt][a] = 0.0;
	}
      }
    }
    
    bool subgame_bucketed = subgame_values.num_bucket_holdings_[p][st] > 0 &&
      subgame_node->LastBetTo() < subgame_values.bucket_thresholds_[st];
    unsigned int subgame_num_holdings;
    if (subgame_bucketed) {
      subgame_num_holdings = subgame_values.num_bucket_holdings_[p][st];
    } else {
      subgame_num_holdings = subgame_values.num_card_holdings_[p][st];
    }
#if 0
    // Can't make this comparison because subgame only has holdings for
    // a particular root board.
    if (subgame_num_holdings != num_holdings_[p][st]) {
      fprintf(stderr, "CFRValues::MergeInto: num_holdings mismatch; st %u\n",
	      st);
      fprintf(stderr, "Subgame: %u\n", subgame_num_holdings);	
      fprintf(stderr, "Merged system: %u\n", num_holdings_[p][st]);
      exit(-1);
    }
#endif
    if (buckets.None(st)) {
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int num_local_boards =
	BoardTree::NumLocalBoards(root_bd_st, root_bd, st);
      if (subgame_num_holdings != num_hole_card_pairs * num_local_boards) {
	fprintf(stderr, "Num holdings didn't match what was expected\n");
	fprintf(stderr, "subgame_num_holdings %u\n", subgame_num_holdings);
	fprintf(stderr, "nhcp %u\n", num_hole_card_pairs);
	fprintf(stderr, "nlb %u\n", num_local_boards);
	fprintf(stderr, "root_bd_st %u st %u\n", root_bd_st, st);
	exit(-1);
      }
      unsigned int a = 0;
      if (chars) {
	unsigned char *cvals = subgame_values.c_values_[p][st][subgame_nt];
	for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
	  unsigned int gbd =
	    BoardTree::GlobalIndex(root_bd_st, root_bd, st, lbd);
	  for (unsigned int hcp = 0; hcp < num_hole_card_pairs; ++hcp) {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      unsigned char v = cvals[a++];
	      unsigned int new_a = gbd * num_hole_card_pairs * num_succs +
		hcp * num_succs + s;
	      c_values_[p][st][full_nt][new_a] = v;
	    }
	  }
	}
      } else if (shorts) {
	unsigned short *svals = subgame_values.s_values_[p][st][subgame_nt];
	for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
	  unsigned int gbd =
	    BoardTree::GlobalIndex(root_bd_st, root_bd, st, lbd);
	  for (unsigned int hcp = 0; hcp < num_hole_card_pairs; ++hcp) {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      unsigned short v = svals[a++];
	      unsigned int new_a = gbd * num_hole_card_pairs * num_succs +
		hcp * num_succs + s;
	      s_values_[p][st][full_nt][new_a] = v;
	    }
	  }
	}
      } else if (ints) {
	int *ivals = subgame_values.i_values_[p][st][subgame_nt];
	for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
	  unsigned int gbd =
	    BoardTree::GlobalIndex(root_bd_st, root_bd, st, lbd);
	  for (unsigned int hcp = 0; hcp < num_hole_card_pairs; ++hcp) {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      int v = ivals[a++];
	      unsigned int new_a = gbd * num_hole_card_pairs * num_succs +
		hcp * num_succs + s;
	      i_values_[p][st][full_nt][new_a] = v;
	    }
	  }
	}
      } else {
	double *dvals = subgame_values.d_values_[p][st][subgame_nt];
	for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
	  unsigned int gbd =
	    BoardTree::GlobalIndex(root_bd_st, root_bd, st, lbd);
	  for (unsigned int hcp = 0; hcp < num_hole_card_pairs; ++hcp) {
	    for (unsigned int s = 0; s < num_succs; ++s) {
	      double v = dvals[a++];
	      unsigned int new_a = gbd * num_hole_card_pairs * num_succs +
		hcp * num_succs + s;
	      d_values_[p][st][full_nt][new_a] = v;
	    }
	  }
	}
      }
    } else {
      bool bucketed = num_bucket_holdings_[p][st] > 0 &&
	full_node->LastBetTo() < bucket_thresholds_[st];
      unsigned int num_holdings;
      if (bucketed) {
	num_holdings = num_bucket_holdings_[p][st];
      } else {
	num_holdings = num_card_holdings_[p][st];
      }
      bool subgame_bucketed = subgame_values.num_bucket_holdings_[p][st] > 0 &&
	subgame_node->LastBetTo() < subgame_values.bucket_thresholds_[st];
      unsigned int subgame_num_holdings;
      if (subgame_bucketed) {
	subgame_num_holdings = subgame_values.num_bucket_holdings_[p][st];
      } else {
	subgame_num_holdings = subgame_values.num_card_holdings_[p][st];
      }
      if (num_holdings != subgame_num_holdings) {
	fprintf(stderr, "CFRValues::MergeInto(): Mismatched num. of buckets\n");
	exit(-1);
      }
      unsigned int num_actions = num_holdings * num_succs;
      if (chars) {
	unsigned char *cvals = subgame_values.c_values_[p][st][subgame_nt];
	for (unsigned int a = 0; a < num_actions; ++a) {
	  c_values_[p][st][full_nt][a] += cvals[a];
	}
      } else if (shorts) {
	unsigned short *svals = subgame_values.s_values_[p][st][subgame_nt];
	for (unsigned int a = 0; a < num_actions; ++a) {
	  s_values_[p][st][full_nt][a] += svals[a];
	}
      } else if (ints) {
	int *ivals = subgame_values.i_values_[p][st][subgame_nt];
	for (unsigned int a = 0; a < num_actions; ++a) {
	  i_values_[p][st][full_nt][a] += ivals[a];
	}
      } else {
	double *dvals = subgame_values.d_values_[p][st][subgame_nt];
	for (unsigned int a = 0; a < num_actions; ++a) {
	  d_values_[p][st][full_nt][a] += dvals[a];
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    MergeInto(full_node->IthSucc(s), subgame_node->IthSucc(s),
	      root_bd_st, root_bd, subgame_values, buckets, final_st);
  }
}

void CFRValues::MergeInto(const CFRValues &subgame_values,
			  unsigned int root_bd, Node *full_root,
			  Node *subgame_root, const Buckets &buckets,
			  unsigned int final_st) {
  unsigned int root_st = full_root->Street();
  MergeInto(full_root, subgame_root, root_st, root_bd, subgame_values,
	    buckets, final_st);
}

void CFRValues::SetValues(Node *node, unsigned char *c_values) {
  unsigned int num_succs = node->NumSuccs();
  if (num_succs <= 1) return;
  unsigned int p = node->PlayerActing();
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  bool bucketed = num_bucket_holdings_[p][st] > 0 &&
    node->LastBetTo() < bucket_thresholds_[st];
  unsigned int num_holdings;
  if (bucketed) {
    num_holdings = num_bucket_holdings_[p][st];
  } else {
    num_holdings = num_card_holdings_[p][st];
  }
  unsigned int num_actions = num_holdings * num_succs;
  for (unsigned int a = 0; a < num_actions; ++a) {
    c_values_[p][st][nt][a] = c_values[a];
  }
}

void CFRValues::SetValues(Node *node, unsigned short *s_values) {
  unsigned int num_succs = node->NumSuccs();
  if (num_succs <= 1) return;
  unsigned int p = node->PlayerActing();
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  bool bucketed = num_bucket_holdings_[p][st] > 0 &&
    node->LastBetTo() < bucket_thresholds_[st];
  unsigned int num_holdings;
  if (bucketed) {
    num_holdings = num_bucket_holdings_[p][st];
  } else {
    num_holdings = num_card_holdings_[p][st];
  }
  unsigned int num_actions = num_holdings * num_succs;
  for (unsigned int a = 0; a < num_actions; ++a) {
    s_values_[p][st][nt][a] = s_values[a];
  }
}

void CFRValues::SetValues(Node *node, int *i_values) {
  unsigned int num_succs = node->NumSuccs();
  if (num_succs <= 1) return;
  unsigned int p = node->PlayerActing();
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  bool bucketed = num_bucket_holdings_[p][st] > 0 &&
    node->LastBetTo() < bucket_thresholds_[st];
  unsigned int num_holdings;
  if (bucketed) {
    num_holdings = num_bucket_holdings_[p][st];
  } else {
    num_holdings = num_card_holdings_[p][st];
  }
  unsigned int num_actions = num_holdings * num_succs;
  for (unsigned int a = 0; a < num_actions; ++a) {
    i_values_[p][st][nt][a] = i_values[a];
  }
}

void CFRValues::SetValues(Node *node, double *d_values) {
  unsigned int num_succs = node->NumSuccs();
  if (num_succs <= 1) return;
  unsigned int p = node->PlayerActing();
  unsigned int st = node->Street();
  unsigned int nt = node->NonterminalID();
  bool bucketed = num_bucket_holdings_[p][st] > 0 &&
    node->LastBetTo() < bucket_thresholds_[st];
  unsigned int num_holdings;
  if (bucketed) {
    num_holdings = num_bucket_holdings_[p][st];
  } else {
    num_holdings = num_card_holdings_[p][st];
  }
  unsigned int num_actions = num_holdings * num_succs;
  for (unsigned int a = 0; a < num_actions; ++a) {
    d_values_[p][st][nt][a] = d_values[a];
  }
}

// Assume full probs and subtree probs are defined for the same players.
// The two betting trees must be identical.
// Assumes both probs objects are bucketed for same streets.
void CFRValues::ReadSubtreeFromFull(Node *full_node, Node *full_subtree_root,
				    Node *subtree_node, Node *subtree_root,
				    Reader ***readers, void ***decompressors,
				    CFRValueType **value_types,
				    unsigned int *num_full_holdings,
				    unsigned int only_p, bool in_subtree) {
  if (full_node->Terminal()) return;
  if (! in_subtree && full_node == full_subtree_root) {
    ReadSubtreeFromFull(full_node, nullptr, subtree_root, nullptr, readers,
			decompressors, value_types, num_full_holdings, only_p,
			true);
    return;
  }
  unsigned int st = full_node->Street();
  unsigned int num_succs = full_node->NumSuccs();
  unsigned int p = full_node->PlayerActing();
  if (streets_[st] && players_[p] && ! (only_p <= 1 && p != only_p) &&
      num_succs > 1) {
    unsigned int snt = kMaxUInt;
    if (in_subtree) {
      snt = subtree_node->NonterminalID();
    }
    Reader *reader = readers[p][st];
    if (reader == nullptr) {
      fprintf(stderr, "CFRValues::ReadSubtreeFromFull(): p %u st %u "
	      "missing file?\n", p, st);
      exit(-1);
    }
    unsigned int num_full_actions = num_full_holdings[st] * num_succs;
    if (in_subtree) {
      // Allocates
      InitializeValuesForReading(p, st, snt, subtree_node, value_types[p][st]);
    }
    if (compressed_streets_[st]) {
      // Issue is that we need to call Decompress() in order to read from
      // the file, but i_values_ has not been allocated so we can't
      // decompress into that.
      fprintf(stderr, "Compression not supported yet\n");
      exit(-1);
#ifdef EJC
      EJDecompressor *decompressor = (EJDecompressor *)decompressors[p][st];
#else
      void *decompressor = decompressors[p][st];
#endif
      // Assumes full tree starts at preflop root
      unsigned int num_local_boards = BoardTree::NumLocalBoards(0, 0, st);
      unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
      unsigned int num_bd_actions = num_hole_card_pairs * num_succs;
      for (unsigned int lbd = 0; lbd < num_local_boards; ++lbd) {
#ifdef EJC
	decompressor->Decompress(&i_values_[p][st][snt][lbd * num_bd_actions],
		 lbd ? &i_values_[p][st][snt][(lbd-1) * num_bd_actions] : NULL,
		 num_bd_actions, num_succs);
#else
	Decompress(decompressor, &i_values_[p][st][snt][lbd * num_bd_actions],
		   lbd ?
		   &i_values_[p][st][snt][(lbd-1) * num_bd_actions] : NULL,
		   num_bd_actions, num_succs);
#endif
      }
    } else {
      if (in_subtree) {
	bool bucketed = num_bucket_holdings_[p][st] > 0 &&
	  full_node->LastBetTo() < bucket_thresholds_[st];
	if (bucketed) {
	  unsigned int num_actions = num_bucket_holdings_[p][st] * num_succs;
	  if (value_types[p][st] == CFR_CHAR) {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      c_values_[p][st][snt][a] = reader->ReadUnsignedCharOrDie();
	    }
	  } else if (value_types[p][st] == CFR_SHORT) {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      s_values_[p][st][snt][a] = reader->ReadShortOrDie();
	    }
	  } else if (value_types[p][st] == CFR_INT) {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      i_values_[p][st][snt][a] = reader->ReadIntOrDie();
	    }
	  } else {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      d_values_[p][st][snt][a] = reader->ReadDoubleOrDie();
	    }
	  }
	} else {
	  unsigned int num_hole_card_pairs = Game::NumHoleCardPairs(st);
	  unsigned char cv = 0;
	  unsigned short sv = 0;
	  int iv = 0;
	  double dv = 0;
	  for (unsigned int a = 0; a < num_full_actions; ++a) {
	    if (value_types[p][st] == CFR_CHAR) {
	      cv = reader->ReadUnsignedCharOrDie();
	    } else if (value_types[p][st] == CFR_SHORT) {
	      sv = reader->ReadUnsignedShortOrDie();
	    } else if (value_types[p][st] == CFR_INT) {
	      iv = reader->ReadIntOrDie();
	    } else {
	      dv = reader->ReadDoubleOrDie();
	    }
	    unsigned int gbd = a / (num_hole_card_pairs * num_succs);
	    bool use_board;
	    if (st == root_bd_st_) {
	      use_board = (gbd == root_bd_);
	    } else {
	      use_board =
		(gbd >= BoardTree::SuccBoardBegin(root_bd_st_, root_bd_, st) &&
		 gbd < BoardTree::SuccBoardEnd(root_bd_st_, root_bd_, st));
	    }
	    if (use_board) {
	      unsigned int lbd = BoardTree::LocalIndex(root_bd_st_, root_bd_,
						       st, gbd);
	      unsigned int rem = a % (num_hole_card_pairs * num_succs);
	      unsigned int la = lbd * num_hole_card_pairs * num_succs + rem;
	      if (value_types[p][st] == CFR_CHAR) {
		c_values_[p][st][snt][la] = cv;
	      } else if (value_types[p][st] == CFR_SHORT) {
		s_values_[p][st][snt][la] = sv;
	      } else if (value_types[p][st] == CFR_INT) {
		i_values_[p][st][snt][la] = iv;
	      } else {
		d_values_[p][st][snt][la] = dv;
	      }
	    }
	  }
	}
      } else {
	// Not in subtree
	bool bucketed = num_bucket_holdings_[p][st] > 0 &&
	  full_node->LastBetTo() < bucket_thresholds_[st];
	if (bucketed) {
	  unsigned int num_actions = num_bucket_holdings_[p][st] * num_succs;
	  if (value_types[p][st] == CFR_CHAR) {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      reader->ReadUnsignedCharOrDie();
	    }
	  } else if (value_types[p][st] == CFR_SHORT) {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      reader->ReadUnsignedShortOrDie();
	    }
	  } else if (value_types[p][st] == CFR_INT) {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      reader->ReadIntOrDie();
	    }
	  } else {
	    for (unsigned int a = 0; a < num_actions; ++a) {
	      reader->ReadDoubleOrDie();
	    }
	  }
	} else {
	  if (value_types[p][st] == CFR_CHAR) {
	    for (unsigned int a = 0; a < num_full_actions; ++a) {
	      reader->ReadUnsignedCharOrDie();
	    }
	  } else if (value_types[p][st] == CFR_SHORT) {
	    for (unsigned int a = 0; a < num_full_actions; ++a) {
	      reader->ReadShortOrDie();
	    }
	  } else if (value_types[p][st] == CFR_INT) {
	    for (unsigned int a = 0; a < num_full_actions; ++a) {
	      reader->ReadIntOrDie();
	    }
	  } else {
	    for (unsigned int a = 0; a < num_full_actions; ++a) {
	      reader->ReadDoubleOrDie();
	    }
	  }
	}
      }
    }
  }
  for (unsigned int s = 0; s < num_succs; ++s) {
    if (in_subtree) {
      ReadSubtreeFromFull(full_node->IthSucc(s), nullptr,
			  subtree_node->IthSucc(s), nullptr, readers,
			  decompressors, value_types, num_full_holdings,
			  only_p, true);
    } else {
      ReadSubtreeFromFull(full_node->IthSucc(s), full_subtree_root,
			  nullptr, subtree_root, readers,
			  decompressors, value_types, num_full_holdings,
			  only_p, false);
    }
  }
}

// Not sure if I can support full_root being anything other than the
// overall root (street 0) of the entire full tree.  Note that we pass
// zero in for root_bd_st to InitializeReader().
void CFRValues::ReadSubtreeFromFull(const char *dir, unsigned int it,
				    Node *full_root, Node *full_subtree_root,
				    Node *subtree_root,
				    const string &action_sequence,
				    unsigned int *num_full_holdings,
				    unsigned int only_p) {
  unsigned int num_players = Game::NumPlayers();
  Reader ***readers = new Reader **[num_players];
  void ***decompressors = new void **[num_players];
  CFRValueType **value_types = new CFRValueType *[num_players];
  unsigned int max_street = Game::MaxStreet();
  for (unsigned int p = 0; p < num_players; ++p) {
    if (only_p != kMaxUInt && p != only_p) {
      readers[p] = nullptr;
      value_types[p] = nullptr;
      decompressors[p] = nullptr;
      continue;
    }
    if (! players_[p]) {
      readers[p] = nullptr;
      value_types[p] = nullptr;
      decompressors[p] = nullptr;
      continue;
    }
    readers[p] = new Reader *[max_street + 1];
    value_types[p] = new CFRValueType[max_street + 1];
    decompressors[p] = new void *[max_street + 1];
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! streets_[st]) {
	readers[p][st] = nullptr;
	decompressors[p][st] = nullptr;
	continue;
      }
      readers[p][st] = InitializeReader(dir, p, st, it, action_sequence,
					full_root->Street(), 0,
					&value_types[p][st]);
      if (compressed_streets_[st]) {
#ifdef EJC
	  decompressors[p][st] = new EJDecompressor(EJDecompressCallback,
	    readers[p][st], NULL);
#else
	  decompressors[p][st] =
	    CreateDecompressor(DecompressCallback, readers[p][st], NULL);
#endif
      } else {
	decompressors[p][st] = nullptr;
      }
    }
  }

  ReadSubtreeFromFull(full_root, full_subtree_root, nullptr, subtree_root,
		      readers, decompressors, value_types, num_full_holdings,
		      only_p, false);
  
  for (unsigned int p = 0; p < num_players; ++p) {
    if (only_p != kMaxUInt && p != only_p) {
      continue;
    }
    if (! players_[p]) {
      continue;
    }
    for (unsigned int st = 0; st <= max_street; ++st) {
      if (! streets_[st]) continue;
#ifdef EJC
      delete (EJDecompressor *)decompressors[p][st];
#else
      if (decompressors[p][st]) DeleteDecompressor(decompressors[p][st]);
#endif
      delete readers[p][st];
    }
    delete [] readers[p];
    delete [] decompressors[p];
  }
  delete [] readers;
  delete [] decompressors;
  for (unsigned int p = 0; p < num_players; ++p) {
    delete [] value_types[p];
  }
  delete [] value_types;
}
