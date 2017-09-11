// Maintains a set of sparse numerical values and dense numerical values
// and a mapping between them.

#include <stdio.h>
#include <stdlib.h>

#include "constants.h"
#include "sparse_and_dense.h"

SparseAndDenseInt::SparseAndDenseInt(void) : SparseAndDense()  {
  sparse_to_dense_ = new unordered_map<unsigned int, unsigned int>;
  unsigned int *block = new unsigned int[kBlockSize];
  dense_to_sparse_ = new vector<unsigned int *>;
  dense_to_sparse_->push_back(block);
}

SparseAndDenseInt::~SparseAndDenseInt(void) {
  delete sparse_to_dense_;
  unsigned int num_blocks = dense_to_sparse_->size();
  for (unsigned int i = 0; i < num_blocks; ++i) {
    delete [] (*dense_to_sparse_)[i];
  }
  delete dense_to_sparse_;
}

unsigned int SparseAndDenseInt::SparseToDense(
				     unsigned long long int ull_sparse)  {
  if (ull_sparse > kMaxUInt) {
    fprintf(stderr, "SparseAndDenseInt::SparseToDense: sparse too big: %llu\n",
	    ull_sparse);
    exit(-1);
  }
  unsigned int sparse = (unsigned int)ull_sparse;
  unordered_map<unsigned int, unsigned int>::iterator it;
  it = sparse_to_dense_->find(sparse);
  if (it == sparse_to_dense_->end()) {
    unsigned int block = num_ / kBlockSize;
    unsigned int dense = num_++;
    if (block >= dense_to_sparse_->size()) {
      dense_to_sparse_->push_back(new unsigned int[kBlockSize]);
    }
    unsigned int index = dense % kBlockSize;
    (*dense_to_sparse_)[block][index] = sparse;
    (*sparse_to_dense_)[sparse] = dense;
    return dense;
  } else {
    return it->second;
  }
}

unsigned long long int SparseAndDenseInt::DenseToSparse(unsigned int dense) {
  unsigned int block = dense / kBlockSize;
  unsigned int index = dense % kBlockSize;
  return (*dense_to_sparse_)[block][index];
}

void SparseAndDenseInt::Clear(void) {
  dense_to_sparse_->clear();
  sparse_to_dense_->clear();
  num_ = 0;
}

SparseAndDenseLong::SparseAndDenseLong(void) : SparseAndDense() {
  sparse_to_dense_ = new unordered_map<unsigned long long int, unsigned int>;
  unsigned long long int *block = new unsigned long long int[kBlockSize];
  dense_to_sparse_ = new vector<unsigned long long int *>;
  dense_to_sparse_->push_back(block);
}

SparseAndDenseLong::~SparseAndDenseLong(void) {
  delete sparse_to_dense_;
  unsigned int num_blocks = dense_to_sparse_->size();
  for (unsigned int i = 0; i < num_blocks; ++i) {
    delete [] (*dense_to_sparse_)[i];
  }
  delete dense_to_sparse_;
}

unsigned int SparseAndDenseLong::SparseToDense(unsigned long long int sparse) {
  unordered_map<unsigned long long int, unsigned int>::iterator it;
  it = sparse_to_dense_->find(sparse);
  if (it == sparse_to_dense_->end()) {
    unsigned int block = num_ / kBlockSize;
    unsigned int dense = num_++;
    if (block >= dense_to_sparse_->size()) {
      dense_to_sparse_->push_back(new unsigned long long int[kBlockSize]);
    }
    unsigned int index = dense % kBlockSize;
    (*dense_to_sparse_)[block][index] = sparse;
    (*sparse_to_dense_)[sparse] = dense;
    return dense;
  } else {
    return it->second;
  }
}

unsigned long long int SparseAndDenseLong::DenseToSparse(unsigned int dense) {
  unsigned int block = dense / kBlockSize;
  unsigned int index = dense % kBlockSize;
  return (*dense_to_sparse_)[block][index];
}

void SparseAndDenseLong::Clear(void) {
  dense_to_sparse_->clear();
  sparse_to_dense_->clear();
  num_ = 0;
}
