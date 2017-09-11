// Maintain a path of nodes in a betting tree.  Typically connecting the root
// to some internal node.  Maintains the succ indices as well as the nodes.

#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <vector>

#include "betting_tree.h"
#include "path.h"

using namespace std;

Path::Path(void) {
  nodes_.reset(new vector<Node *>);
  succs_.reset(new vector<unsigned int>);
}

Path::Path(const Path &p) {
  unsigned int sz = p.Size();
  nodes_.reset(new vector<Node *>(sz));
  succs_.reset(new vector<unsigned int>(sz));
  for (unsigned int i = 0; i < sz; ++i) {
    (*nodes_)[i] = p.IthNode(i);
    (*succs_)[i] = p.IthSucc(i);
  }
}
