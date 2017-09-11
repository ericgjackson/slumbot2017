#ifndef _OLS_H_
#define _OLS_H_

void GaussianElimination(double *x, unsigned int num_rows,
			 unsigned int num_cols);
double *Invert(double *x, unsigned int dim);
double *MVOLS(double *x, double *y, unsigned int n, unsigned int d);
double Eval(double *x, double *y, unsigned int n, unsigned int d,
	    double *params);

#endif
