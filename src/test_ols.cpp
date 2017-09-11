#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ols.h"

#if 0
static void Show(double **x, unsigned int num_rows, unsigned int num_cols) {
  for (unsigned int r = 0; r < num_rows; ++r) {
    for (unsigned int c = 0; c < num_cols; ++c) {
      if (c > 0) printf("\t");
      printf("%f", x[r][c]);
    }
    printf("\n");
  }
}
#endif

static void Show(double *x, unsigned int num_rows, unsigned int num_cols) {
  for (unsigned int r = 0; r < num_rows; ++r) {
    for (unsigned int c = 0; c < num_cols; ++c) {
      if (c > 0) printf("\t");
      printf("%f", x[r * num_cols + c]);
    }
    printf("\n");
  }
}

int main(int argc, char *argv[]) {
  unsigned int num_rows = 3, num_cols = 4;
  double *x = new double[num_rows * num_cols];
  x[0] = 2;
  x[1] = 1;
  x[2] = -1;
  x[3] = 8;
  x[4] = -3;
  x[5] = -1;
  x[6] = 2;
  x[7] = -11;
  x[8] = -2;
  x[9] = 1;
  x[10] = 2;
  x[11] = -3;

  Show(x, num_rows, num_cols);
  printf("-------------------------------------\n");
  GaussianElimination(x, num_rows, num_cols);
  Show(x, num_rows, num_cols);
  printf("-------------------------------------\n");

  num_rows = 3;
  double *m = new double[num_rows * num_rows];
  m[0] = 2;
  m[1] = -1;
  m[2] = 0;
  m[3] = -1;
  m[4] = 2;
  m[5] = -1;
  m[6] = 0;
  m[7] = -1;
  m[8] = 2;
  double *i = Invert(m, num_rows);
  Show(i, num_rows, num_rows);
  delete [] i;

  {
    // Do a univariate case
    unsigned int n = 2, d = 2;
    double *xs = new double[n * d];
    for (unsigned int i = 0; i < n; ++i) {
      xs[i * d + d - 1] = 1.0;
    }
    xs[0] = 1.0;
    xs[2] = 2.0;
    double *ys = new double[n];
    ys[0] = 2.0;
    ys[1] = 3.0;
    double *params = MVOLS(xs, ys, n, d);
    printf("Params: %f %f\n", params[0], params[1]);
  }

  {
    unsigned int n = 3, d = 3;
    double *xs = new double[n * d];
    for (unsigned int i = 0; i < n; ++i) {
      xs[i * d + d - 1] = 1.0;
    }
    double *ys = new double[n];
    xs[0] = 1.0;
    xs[1] = 0;
    xs[3] = 2.0;
    xs[4] = 0.1;
    xs[6] = 3.0;
    xs[7] = -0.1;
    ys[0] = 0;
    ys[1] = 10.0;
    ys[2] = 11.0;
    double *params = MVOLS(xs, ys, n, d);
    printf("Params: %f %f %f\n", params[0], params[1], params[2]);
    double rmse = Eval(xs, ys, n, d, params);
    printf("RMSE: %f\n", rmse);
    printf("-------------\n");
    double *params2 = new double[d];
    for (unsigned int f = 0; f < d; ++f) {
      for (int l = -1; l <= 1; ++l) {
	for (unsigned int f1 = 0; f1 < d; ++f1) {
	  if (f1 == f) params2[f1] = params[f1] + l * 0.001;
	  else         params2[f1] = params[f1];
	}
	double rmse = Eval(xs, ys, n, d, params2);
	printf("Params: %f %f %f\n", params2[0], params2[1], params2[2]);
	printf("RMSE: %f\n", rmse);
      }
    }
  }
}
