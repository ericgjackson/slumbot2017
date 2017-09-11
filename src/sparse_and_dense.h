#ifndef _SPARSE_AND_DENSE_H_
#define _SPARSE_AND_DENSE_H_

#include <unordered_map>
#include <vector>

using namespace std;

class SparseAndDense {
 public:
  SparseAndDense(void) {num_ = 0;}
  virtual ~SparseAndDense(void) {}
  // Adds a sparse value, returns the corresponding dense value
  virtual unsigned int SparseToDense(unsigned long long int sparse) = 0;
  virtual unsigned long long int DenseToSparse(unsigned int dense) = 0;
  virtual void Clear(void) = 0;
  unsigned int Num(void) const {return num_;}

 protected:
  unsigned int num_;
};

class SparseAndDenseInt : public SparseAndDense {
public:
  SparseAndDenseInt(void);
  ~SparseAndDenseInt(void);
  unsigned int SparseToDense(unsigned long long int sparse);
  // Return an unsigned long long int even though sparse values can be
  // represented as unsigned ints.  Caller can cast as needed.
  unsigned long long int DenseToSparse(unsigned int dense);
  void Clear(void);
private:
  static const int kBlockSize = 1000000;

  unordered_map<unsigned int, unsigned int> *sparse_to_dense_;
  vector<unsigned int *> *dense_to_sparse_;
};

class SparseAndDenseLong : public SparseAndDense {
public:
  SparseAndDenseLong(void);
  ~SparseAndDenseLong(void);
  // Adds a sparse value, returns the corresponding dense value
  unsigned int SparseToDense(unsigned long long int sparse);
  unsigned long long int DenseToSparse(unsigned int dense);
  void Clear(void);
private:
  static const int kBlockSize = 1000000;

  unordered_map<unsigned long long int, unsigned int> *sparse_to_dense_;
  vector<unsigned long long int *> *dense_to_sparse_;
};

#endif
