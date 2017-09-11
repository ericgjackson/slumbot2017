#ifndef _PATH_H_
#define _PATH_H_

#include <memory>
#include <vector>

using namespace std;

class Node;

class Path {
public:
  Path(void);
  Path(const Path &p);
  ~Path(void) {}
  unsigned int Size(void) const {return nodes_->size();}
  Node *IthNode(unsigned int i) const {return (*nodes_)[i];}
  unsigned int IthSucc(unsigned int i) const {return (*succs_)[i];}
  void Push(Node *n) {
    nodes_->push_back(n);
    succs_->push_back(kMaxUInt);
  }
  void Pop(void) {
    nodes_->pop_back();
    succs_->pop_back();
  }
  void SetLastSucc(unsigned int s) {
    (*succs_)[succs_->size() - 1] = s;
  }
  void SetLastNode(Node *n) {
    (*nodes_)[nodes_->size() - 1] = n;
  }
  // Assumes path not empty
  Node *Last(void) const {return (*nodes_)[nodes_->size() - 1];}
  vector<Node *> *Nodes(void) const {return nodes_.get();}
private:
  unique_ptr< vector<Node *> > nodes_;
  unique_ptr< vector<unsigned int> > succs_;
};

#endif
