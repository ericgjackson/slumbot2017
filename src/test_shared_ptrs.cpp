#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <vector>

using namespace std;

class MyNode {
public:
  MyNode(int key, const vector< shared_ptr<MyNode> > &succs);
  ~MyNode(void);
private:
  int key_;
  shared_ptr<MyNode> *my_succs_;
};

MyNode::MyNode(int key, const vector< shared_ptr<MyNode> > &succs) {
  unsigned int num_succs = succs.size();
  my_succs_ = new shared_ptr<MyNode>[num_succs];
  for (unsigned int s = 0; s < num_succs; ++s) {
    my_succs_[s] = succs[s];
  }
  key_ = key;
}

MyNode::~MyNode(void) {
  fprintf(stderr, "Deleting key %i\n", key_);
  delete [] my_succs_;
  fprintf(stderr, "Deleted key %i\n", key_);
}

int main(int argc, char *argv[]) {
  vector< shared_ptr<MyNode> > v;
  shared_ptr<MyNode> n3(new MyNode(3, v));
  v.push_back(n3);
  shared_ptr<MyNode> n2(new MyNode(2, v));
  shared_ptr<MyNode> n1(new MyNode(1, v));
  v.clear();
  v.push_back(n1);
  v.push_back(n2);
  shared_ptr<MyNode> n0(new MyNode(0, v));
}
