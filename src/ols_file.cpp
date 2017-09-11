#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "io.h"
#include "ols.h"

static void Usage(const char *prog_name) {
  fprintf(stderr, "USAGE: %s <x-file> <y-file> <n> <d>\n", prog_name);
  exit(-1);
}

int main(int argc, char *argv[]) {
  if (argc != 5) Usage(argv[0]);
  const char *x_filename = argv[1];
  const char *y_filename = argv[2];
  unsigned int n, d;
  if (sscanf(argv[3], "%u", &n) != 1) Usage(argv[0]);
  if (sscanf(argv[4], "%u", &d) != 1) Usage(argv[0]);
  Reader x_reader(x_filename);
  if (x_reader.FileSize() != (long long int)(n * d * sizeof(double))) {
    fprintf(stderr, "Unexpected x file size\n");
    exit(-1);
  }
  Reader y_reader(y_filename);
  if (y_reader.FileSize() != (long long int)(n * sizeof(double))) {
    fprintf(stderr, "Unexpected y file size\n");
    exit(-1);
  }
  unsigned int num_x = n * d;
  double *x = new double[num_x];
  for (unsigned int i = 0; i < num_x; ++i) {
    x[i] = x_reader.ReadDoubleOrDie();
  }
  double *y = new double[n];
  for (unsigned int i = 0; i < n; ++i) {
    y[i] = y_reader.ReadDoubleOrDie();
  }

  double *params = MVOLS(x, y, n, d);
  double rmse = Eval(x, y, n, d, params);
  printf("RMSE: %f\n", rmse);
}

