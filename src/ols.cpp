// Should detect non-invertible matrices.  Isn't there a test during
// Gaussian elimination?

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ols.h"

// x should be an augmented square matrix.  It should have num_rows rows and
// num_cols columns.
// x gets modified
void GaussianElimination(double *x, unsigned int num_rows,
			 unsigned int num_cols) {
  for (unsigned int r = 0; r < num_rows - 1; ++r) {
    double f = x[r * num_cols + r];
    if (f == 0) {
      // This shouldn't be fatal, should it?
      fprintf(stderr, "x[%u][%u] is zero\n", r, r);
      exit(-1);
    }
    for (unsigned int r2 = r + 1; r2 < num_rows; ++r2) {
      double f2 = x[r2 * num_cols + r];
      double ratio = -f2/f;
      for (unsigned int c = r; c < num_cols; ++c) {
	x[r2 * num_cols + c] += ratio * x[r * num_cols + c];
      }
    }
  }
  for (int r = num_rows - 1; r >= 1; --r) {
    for (int r2 = r - 1; r2 >= 0; --r2) {
      double ratio = -x[r2 * num_cols + r] / x[r * num_cols + r];
      x[r2 * num_cols + r] += ratio * x[r * num_cols + r];
      for (unsigned int c = num_rows; c < num_cols; ++c) {
	x[r2 * num_cols + c] += ratio * x[r * num_cols + c];
      }
    }
  }

  for (unsigned int r = 0; r < num_rows; ++r) {
    double ratio = 1.0 / x[r * num_cols + r];
    x[r * num_cols + r] *= ratio;
    for (unsigned int c = num_rows; c < num_cols; ++c) {
      x[r * num_cols + c] *= ratio;
    }
  }
}

double *Invert(double *x, unsigned int dim) {
  unsigned int num_cols = 2 * dim;
  double *a = new double[dim * num_cols];
  for (unsigned int r = 0; r < dim; ++r) {
    for (unsigned int c = 0; c < num_cols; ++c) {
      if (c < dim) {
	a[r * num_cols + c] = x[r * dim + c];
      } else {
	if (c - dim == r) {
	  a[r * num_cols + c] = 1;
	} else {
	  a[r * num_cols + c] = 0;
	}
      }
    }
  }
  GaussianElimination(a, dim, num_cols);
  double *i = new double[dim * dim];
  for (unsigned int r = 0; r < dim; ++r) {
    for (unsigned int c = 0; c < dim; ++c) {
      i[r * dim + c] = a[r * num_cols + dim + c];
    }
  }
  delete [] a;

  return i;
}

// x has n rows and d columns; y has n rows
//
// B = (X'X)-1X'y
//   = Sum(xixi')-1 Sum(xiyi)
// Suppose there are n observations and d dimensions
// X is nxd
// Y is nx1
double *MVOLS(double *x, double *y, unsigned int n, unsigned int d) {
  double *xtx = new double[d * d];
  for (unsigned int f1 = 0; f1 < d; ++f1) {
    for (unsigned int f2 = f1; f2 < d; ++f2) {
      double sum = 0;
      for (unsigned int i = 0; i < n; ++i) {
	sum += x[i * d + f1] * x[i * d + f2];
      }
      xtx[f1 * d + f2] = sum;
      if (f2 > f1) xtx[f2 * d + f1] = sum;
    }
  }

  double *i = Invert(xtx, d);
  delete [] xtx;

  double *xty = new double[d];
  for (unsigned int f = 0; f < d; ++f) {
    double sum = 0;
    for (unsigned int i = 0; i < n; ++i) {
      sum += x[i * d + f] * y[i];
    }
    xty[f] = sum;
  }

  double *params = new double[d];
  for (unsigned int f1 = 0; f1 < d; ++f1) {
    double sum = 0;
    for (unsigned int f2 = 0; f2 < d; ++f2) {
      sum += i[f1 * d + f2] * xty[f2];
    }
    params[f1] = sum;
  }
  delete [] i;
  delete [] xty;

  return params;
}

// Returns RMSE
double Eval(double *x, double *y, unsigned int n, unsigned int d,
	    double *params) {
  double sum_sqd_err = 0;
  for (unsigned int i = 0; i < n; ++i) {
    double this_y = y[i];
    double pred = 0;
    for (unsigned int f = 0; f < d; ++f) {
      pred += params[f] * x[i * d + f];
    }
    double err = this_y - pred;
    // fprintf(stderr, "y %f pred %f err %f\n", this_y, pred, err);
    sum_sqd_err += err * err;
  }
  return sqrt(sum_sqd_err / n);
}
