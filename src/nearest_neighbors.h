#ifndef _NEAREST_NEIGHBORS_H_
#define _NEAREST_NEIGHBORS_H_

#include <vector>

using namespace std;

class CardAbstraction;
class Reader;
class RuntimeConfig;

class NearestNeighbors {
public:
  NearestNeighbors(const CardAbstraction *ca, const RuntimeConfig *rc,
		   const unsigned int *num_buckets);
  ~NearestNeighbors(void);
  void GetNeighbors(unsigned int b, vector<unsigned int> *v);
private:
  bool nn_disk_;
  Reader *reader_;
  unsigned int max_neighbors_;
  unsigned int *neighbors_;
  unsigned int *offsets_;
};

#endif
