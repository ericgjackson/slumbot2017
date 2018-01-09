#ifndef _JOINT_REACH_PROBS_H_
#define _JOINT_REACH_PROBS_H_

using namespace std;

class BettingAbstraction;
class BettingTree;
class CardAbstraction;
class CFRConfig;

class JointReachProbs {
public:
  JointReachProbs(const CardAbstraction &ca,const BettingAbstraction &ba,
		  const CFRConfig &cc, const unsigned int *num_buckets,
		  unsigned int it, unsigned int final_st);
  ~JointReachProbs(void);
  float OurReachProb(unsigned int p, unsigned int st, unsigned int nt,
		     unsigned int b) const {
    return our_reach_probs_[p][st][nt][b];
  }
  // This isn't the opponent's probability of reaching this state with bucket
  // b.  This is the opponent's probability of reaching this state when *we*
  // have bucket b.
  float OppReachProb(unsigned int p, unsigned int st, unsigned int nt,
		     unsigned int b) const {
    return opp_reach_probs_[p][st][nt][b];
  }
  float JointReachProb(unsigned int p, unsigned int st, unsigned int nt,
		       unsigned int b) const {
    return our_reach_probs_[p][st][nt][b] *
      opp_reach_probs_[p][st][nt][b];
  }
private:
  unsigned int final_st_;
  unique_ptr<BettingTree> betting_tree_;
  float ****our_reach_probs_;
  float ****opp_reach_probs_;
};

#endif
