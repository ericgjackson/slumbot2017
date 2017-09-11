#ifndef _ROLLOUT_H_
#define _ROLLOUT_H_

#include "cards.h"

short *RiverHandStrength(const Card *board);
short *ComputeRollout(unsigned int st, double *percentiles,
		      unsigned int num_percentiles,
		      double squashing);

#endif
