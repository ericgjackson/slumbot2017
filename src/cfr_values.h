#ifndef _CFR_VALUES_H_
#define _CFR_VALUES_H_

#include <memory>

using namespace std;

class BettingTree;
class Buckets;
class CardAbstraction;
class Node;
class Reader;
class Writer;

#define EJC 1

class CFRValues {
public:
  CFRValues(const bool *players, bool sumprobs, bool *streets,
	    const BettingTree *betting_tree, unsigned int root_bd,
	    unsigned int root_bd_st, const CardAbstraction &card_abstraction,
	    const Buckets &buckets, const bool *compressed_streets);
  ~CFRValues(void);
  bool Ints(unsigned int p, unsigned int st) const {
    return i_values_ && i_values_[p] && i_values_[p][st];
  }
  bool Doubles(unsigned int p, unsigned int st) const {
    return d_values_ && d_values_[p] && d_values_[p][st];
  }
  void Values(unsigned int p, unsigned int st, unsigned int nt,
	      int **i_values) const {
    *i_values = i_values_[p][st][nt];
  }
  void Values(unsigned int p, unsigned int st, unsigned int nt,
	      double **d_values) const {
    *d_values = d_values_[p][st][nt];
  }
  void SetValues(Node *node, int *i_values);
  void SetValues(Node *node, double *d_values);
  double Prob(unsigned int p, unsigned int st, unsigned int nt,
	      unsigned int offset, unsigned int s, 
	      unsigned int num_succs, unsigned int dsi) const {
    if (i_values_ && i_values_[p] && i_values_[p][st]) {
      double sum = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	sum += i_values_[p][st][nt][offset + s];
      }
      if (sum == 0) {
	if (s == dsi) return 1.0;
	else          return 0;
      } else {
	return i_values_[p][st][nt][offset + s] / sum;
      }
    } else {
      double sum = 0;
      for (unsigned int s = 0; s < num_succs; ++s) {
	sum += d_values_[p][st][nt][offset + s];
      }
      if (sum == 0) {
	if (s == dsi) return 1.0;
	else          return 0;
      } else {
	return d_values_[p][st][nt][offset + s] / sum;
      }
    }
  }
  void AllocateAndClearInts(Node *node, unsigned int only_p);
  void AllocateAndClearDoubles(Node *node, unsigned int only_p);
  void DeleteBelow(Node *node);
  void ReadNode(Node *node, Reader *reader, void *decompressor,
		unsigned int num_holdings, bool ints, unsigned int offset);
  void Read(const char *dir, unsigned int it, Node *root,
	    unsigned int subtree_nt, unsigned int only_p);
  void DeleteWriters(Writer ***writers, void ***compressors) const;
  Writer ***InitializeWriters(const char *dir, unsigned int it,
			      unsigned int root_st, unsigned int subtree_nt,
			      unsigned int only_p,
			      void ****compressors) const;
  void WriteNode(Node *node, Writer *writer, void *compressor,
		 unsigned int num_holdings, unsigned int offset) const;
  void Write(Node *node, Writer ***writers, void ***compressors) const;
  void Write(const char *dir, unsigned int it, Node *root,
	     unsigned int subtree_nt, unsigned int only_p) const;
#if 0
  void Write(const char *dir, unsigned int it, Node *root,
	     unsigned int target_bd, unsigned int subtree_nt,
	     unsigned int only_p) const;
#endif
  void MergeInto(const CFRValues &subgame_values, unsigned int root_bd,
		 Node *full_root, Node *subgame_root, const Buckets &buckets,
		 unsigned int final_st);
  void ReadSubtreeFromFull(const char *dir, unsigned int it,
			   Node *full_root, Node *full_subtree_root,
			   Node *subtree_root,
			   unsigned int *num_full_holdings,
			   unsigned int only_p);
  bool Players(unsigned int p) const {return players_[p];}
  unsigned int NumNonterminals(unsigned int p, unsigned int st) const {
    return num_nonterminals_[p][st];
  }

protected:
  void AllocateAndClear(Node *node, bool ints, unsigned int only_p);
  Reader *InitializeReader(const char *dir, unsigned int p, unsigned int st,
			   unsigned int it, unsigned int subtree_st,
			   unsigned int subtree_nt, unsigned int root_bd_st,
			   unsigned int root_bd, bool *int_type);
  void InitializeValuesForReading(unsigned int p, unsigned int st,
				  unsigned int nt, Node *node, bool ints);
  void Read(Node *node, Reader ***readers, void ***decompressors, bool **ints,
	    unsigned int only_p);
  void MergeInto(Node *full_node, Node *subgame_node, unsigned int root_bd_st,
		 unsigned int root_bd, const CFRValues &subgame_values,
		 const Buckets &buckets, unsigned int final_st);
  void ReadSubtreeFromFull(Node *full_node, Node *full_subtree_root,
			   Node *subtree_node, Node *subtree_root,
			   Reader ***readers, void ***decompressors,
			   bool **ints, unsigned int *num_full_holdings,
			   unsigned int only_p, bool in_subtree);
  
  unique_ptr<bool []> players_;
  bool sumprobs_;
  unique_ptr<bool []> streets_;
  unsigned int **num_nonterminals_;
  unsigned int root_bd_st_;
  unsigned int root_bd_;
  unique_ptr<unsigned int []> bucket_thresholds_;
  // Index by p1 and street.
  unsigned int **num_card_holdings_;
  unsigned int **num_bucket_holdings_;
  // Index by p1, street, NT and index
  // Index could be board/hole-card-pair/succ, or bucket/succ
  int ****i_values_;
  double ****d_values_;
  unique_ptr<bool []> compressed_streets_;
#ifdef EJC
  long long int **new_distributions_;
#else
  int64_t **new_distributions_;
#endif
};

#endif
