#ifndef _DYNAMIC_CBR_H_
#define _DYNAMIC_CBR_H_

class Buckets;
class CardAbstraction;
class CFRValues;
class HandTree;
class Node;

class DynamicCBR {
public:
  DynamicCBR(void);
  ~DynamicCBR(void);
  double *Compute(Node *node, double **reach_probs, unsigned int gbd,
		  HandTree *hand_tree, const CFRValues *sumprobs,
		  unsigned int root_bd_st, unsigned int root_bd,
		  const Buckets &buckets,
		  const CardAbstraction &card_abstraction,
		  unsigned int target_p, bool cfrs);
private:
  double *OurChoice(Node *node, unsigned int lbd, unsigned int p,
		    double *opp_probs, double sum_opp_probs,
		    double *total_card_probs, HandTree *hand_tree,
		    const CFRValues *sumprobs, unsigned int root_bd_st,
		    unsigned int root_bd, const Buckets &buckets,
		    const CardAbstraction &card_abstraction);
  double *OppChoice(Node *node, unsigned int lbd, unsigned int p,
		    double *opp_probs, double sum_opp_probs,
		    double *total_card_probs, HandTree *hand_tree,
		    const CFRValues *sumprobs, unsigned int root_bd_st,
		    unsigned int root_bd, const Buckets &buckets,
		    const CardAbstraction &card_abstraction);
  void SetStreetBuckets(unsigned int st, unsigned int gbd,
			const Buckets &buckets, const CanonicalCards *hands);
  double *StreetInitial(Node *node, unsigned int plbd, unsigned int p,
			double *opp_probs, HandTree *hand_tree,
			const CFRValues *sumprobs, unsigned int root_bd_st,
			unsigned int root_bd, const Buckets &buckets,
			const CardAbstraction &card_abstraction);
  double *Process(Node *node, unsigned int lbd, unsigned int p,
		  double *opp_probs, double sum_opp_probs,
		  double *total_card_probs, HandTree *hand_tree,
		  const CFRValues *sumprobs, unsigned int root_bd_st,
		  unsigned int root_bd, const Buckets &buckets,
		  const CardAbstraction &card_abstraction,
		  unsigned int last_st);
  double *Compute(Node *node, unsigned int p, double *opp_probs,
		  unsigned int gbd, HandTree *hand_tree,
		  const CFRValues *sumprobs, unsigned int root_bd_st,
		  unsigned int root_bd, const Buckets &buckets,
		  const CardAbstraction &card_abstraction);

  bool cfrs_;
  unsigned int **street_buckets_;
};

#endif
