#ifndef _VCFR_STATE_H_
#define _VCFR_STATE_H_

class VCFRState {
 public:
  VCFRState(double *opp_probs, unsigned int **street_buckets,
	    const HandTree *hand_tree, unsigned int p);
  VCFRState(double *opp_probs, const HandTree *hand_tree, 
	    unsigned int lbd, const string &action_sequence,
	    unsigned int root_bd, unsigned int root_bd_st,
	    unsigned int **street_buckets, unsigned int p);
  VCFRState(double *opp_probs, double *total_card_probs,
	    const HandTree *hand_tree, unsigned int st, unsigned int lbd,
	    const string &action_sequence, unsigned int root_bd,
	    unsigned int root_bd_st, unsigned int **street_buckets,
	    unsigned int p);
  VCFRState(const VCFRState &pred, Node *node, unsigned int s);
  VCFRState(const VCFRState &pred, Node *node, unsigned int s,
	    double *opp_probs, double sum_opp_probs, double *total_card_probs);
  virtual ~VCFRState(void);
  double *OppProbs(void) const {return opp_probs_;}
  double SumOppProbs(void) const {return sum_opp_probs_;}
  double *TotalCardProbs(void) const {return total_card_probs_;}
  unsigned int **StreetBuckets(void) const {return street_buckets_;}
  const string &ActionSequence(void) const {return action_sequence_;}
  const HandTree *GetHandTree(void) const {return hand_tree_;}
  unsigned int P(void) const {return p_;}
  unsigned int RootBd(void) const {return root_bd_;}
  unsigned int RootBdSt(void) const {return root_bd_st_;}
  void SetOppProbs(double *opp_probs) {opp_probs_ = opp_probs;}
  void SetP(unsigned int p) {p_ = p;}
 protected:
  double *opp_probs_;
  double sum_opp_probs_;
  double *total_card_probs_;
  unsigned int **street_buckets_;
  string action_sequence_;
  const HandTree *hand_tree_;
  unsigned int p_;
  unsigned int root_bd_;
  unsigned int root_bd_st_;
};

unsigned int **AllocateStreetBuckets(void);
void DeleteStreetBuckets(unsigned int **street_buckets);
double *AllocateOppProbs(bool initialize);

#endif
