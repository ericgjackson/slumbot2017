#ifndef _CARD_ABSTRACTION_H_
#define _CARD_ABSTRACTION_H_

#include <string>
#include <vector>

using namespace std;

class Params;

class CardAbstraction {
 public:
  CardAbstraction(const Params &params);
  ~CardAbstraction(void);
  const string &CardAbstractionName(void) const {return card_abstraction_name_;}
  const vector<string> &Bucketings(void) const {return bucketings_;}
  const string &Bucketing(unsigned int st) const {return bucketings_[st];}
  unsigned int BucketThreshold(unsigned int st) const {
    return bucket_thresholds_[st];
  }
 private:
  string card_abstraction_name_;
  vector<string> bucketings_;
  unsigned int *bucket_thresholds_;
};

#endif
