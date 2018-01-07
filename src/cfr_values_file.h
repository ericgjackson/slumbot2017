#ifndef _CFR_VALUES_FILE_H_
#define _CFR_VALUES_FILE_H_

#include "cfr_value_type.h"
#include "prob_method.h"

class BettingAbstraction;
class BettingTree;
class CardAbstraction;
class CFRConfig;
class CFRValues;
class Reader;

class CFRValuesFile {
public:
  CFRValuesFile(const bool *players, bool *streets,
		const CardAbstraction &card_abstraction,
		const BettingAbstraction &betting_abstraction,
		const CFRConfig &cfr_config, unsigned int asym_p,
		unsigned int it, unsigned int endgame_st,
		const BettingTree *betting_tree,
		const unsigned int *num_buckets);
  virtual ~CFRValuesFile(void);
  void Probs(unsigned int p, unsigned int st, unsigned int nt,
	     unsigned int h, unsigned int num_succs, unsigned int dsi,
	     double *probs) const;
  void ReadPureSubtree(Node *whole_node, BettingTree *subtree,
		       CFRValues *regrets);
private:
  void InitializeOffsets(Node *node, unsigned long long int **current,
			 bool ***seen);
  void ReadPureSubtree(Node *whole_node, Node *subtree_node,
		       CFRValues *regrets);
  
  unsigned int *num_holdings_;
  ProbMethod **methods_;
  CFRValueType **value_types_;
  Reader ***readers_;
  unsigned long long int ***offsets_;
};

#endif
