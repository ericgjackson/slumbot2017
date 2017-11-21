// Want to see if I can use templates more aggressively.
// Regrets can be chars, shorts, ints or doubles.
// Sumprobs can be some or all of these things as well.
// Different streets may have different types.

#include <math.h> // lrint()
#include <stdio.h>
#include <stdlib.h>

#include <memory>

#include "cfr_street_values.h"
#include "cfr_utils2.h"

using namespace std;

int main(int argc, char *argv[]) {
  unsigned int num_succs = 3;
  CFRStreetValues<int> street_values(10, num_succs);
  street_values.Allocate();
  street_values.Clear();
  unique_ptr<double []> probs(new double[num_succs]);
  street_values.Probs(0, probs.get());
  printf("h %u probs %f %f %f\n", 0, probs[0], probs[1], probs[2]);
}
