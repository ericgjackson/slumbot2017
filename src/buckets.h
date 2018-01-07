#ifndef _BUCKETS_H_
#define _BUCKETS_H_

class CardAbstraction;
class Reader;

class Buckets {
public:
  Buckets(void);
  Buckets(const CardAbstraction &ca, bool numb_only);
  Buckets(const CardAbstraction &ca, unsigned int final_street);
  virtual ~Buckets(void);
  bool None(unsigned int st) const {return none_[st];}
  virtual unsigned int Bucket(unsigned int st, unsigned int h) const {
    if (short_buckets_[st]) {
      return short_buckets_[st][h];
    } else {
      return int_buckets_[st][h];
    }
  }
  const unsigned int *NumBuckets(void) const {return num_buckets_;}
  unsigned int NumBuckets(unsigned int st) const {return num_buckets_[st];}
 protected:
  bool *none_;
  unsigned short **short_buckets_;
  unsigned int **int_buckets_;
  unsigned int *num_buckets_;
};

class BucketsFile : public Buckets {
public:
  BucketsFile(const CardAbstraction &ca);
  ~BucketsFile(void);
  unsigned int Bucket(unsigned int st, unsigned int h) const;
private:
  bool *shorts_;
  Reader **readers_;
};

#endif
