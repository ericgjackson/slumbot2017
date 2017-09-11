#include <stdio.h>
#include <stdlib.h>

#include "constants.h"
#include "rand.h"
#include "univariate_kmeans.h"

UnivariateKMeans::UnivariateKMeans(unsigned int num_objects, int *objects) {
  num_objects_ = num_objects;
  objects_ = objects;
  assignments_ = new unsigned int[num_objects_];
  means_ = nullptr;
  cluster_sizes_ = nullptr;
}

UnivariateKMeans::~UnivariateKMeans(void) {
  delete [] assignments_;
  delete [] means_;
  delete [] cluster_sizes_;
}

unsigned int UnivariateKMeans::BinarySearch(double r,
					unsigned int begin, unsigned int end,
					long long int *cum_distance_to_nearest,
					bool *used) {
  if (end == begin + 1) {
    // Is it possible that we will select a used object?
    if (used[begin]) {
      fprintf(stderr, "Ended up with used object?!?\n");
      fprintf(stderr, "r %f\n", r);
      fprintf(stderr, "Prev %lli\n", cum_distance_to_nearest[begin]);
      fprintf(stderr, "Next %lli\n", cum_distance_to_nearest[end]);
      fprintf(stderr, "Begin %u end %u\n", begin, end);
      exit(-1);
    }
    return begin;
  } else {
    // The median may be an already used object, but cum_distance_to_nearest
    // is set appropriately.
    unsigned int median = (end + begin) / 2;
    long long int median_val = cum_distance_to_nearest[median];
    if (r < median_val) {
      if (median == 0) return 0;
      // Find the previous object that is not used
      int prev;
      for (prev = median - 1; prev >= 0; --prev) {
	if (! used[prev]) {
	  if (r >= cum_distance_to_nearest[prev]) {
	    return median;
	  } else {
	    return BinarySearch(r, begin, median, cum_distance_to_nearest,
				used);
	  }
	}
      }
      // If we get here, median is the first unused object
      return median;
    } else if (r > median_val) {
      return BinarySearch(r, median + 1, end, cum_distance_to_nearest, used);
      // r is >= median_val
    } else {
      // r is exactly equal to median_val!  (Quite a coincidence.)
      // First look for the first unused object and or before median.  If
      // we find one, return it.
      for (int i = median; i >= 0; --i) {
	if (! used[i]) return i;
      }
      // Coincidences on top of coincidences!  There was no prior object.
      // Look for a later object.
      for (int i = median + 1; i < (int)end; ++i) {
	if (! used[i]) return i;
      }
      fprintf(stderr, "Shouldn't get here\n");
      exit(-1);
    }
  }
}

// Use the KMeans++ method of seeding
// Should maintain cum_distance_to_nearest, do binary search on it
// for lookup.  Still need O(n) update to distance_to_nearest though.
// Should I compute squared distance in this function?
// Should I gracefully handle case where there are more clusters than objects?
void UnivariateKMeans::SeedPlusPlus(void) {
  bool *used = new bool[num_objects_];
  for (unsigned int o = 0; o < num_objects_; ++o) used[o] = false;
  // For the first centroid, choose one of the input objects at random
  unsigned int o = RandBetween(0, num_objects_ - 1);
  // fprintf(stderr, "c 0 o %u\n", o);
  used[o] = true;
  means_[0] = objects_[o];
  // The distance from each object to its nearest centroid.  Not maintained
  // for objects that are centroids (but it would be zero if it were
  // maintained).
  int *distance_to_nearest = new int[num_objects_];
  long long int *cum_distance_to_nearest = new long long int[num_objects_];
  // The sum of the distances of all objects to their nearest centroid.
  // Excludes those objects that are centroids.
  long long int sum_dists = 0;
  long long int cum_dist = 0;
  for (unsigned int o = 0; o < num_objects_; ++o) {
    if (used[o]) {
      cum_distance_to_nearest[o] = cum_dist;
      continue;
    }
    int delta = objects_[o] - means_[0];
    int dist = delta >= 0 ? delta : -delta;
    distance_to_nearest[o] = dist;
    cum_dist += dist;
    cum_distance_to_nearest[o] = cum_dist;
    sum_dists += dist;
  }
  for (unsigned int c = 1; c < num_clusters_; ++c) {
    // fprintf(stderr, "c %u\n", c);
    if (c % 1000 == 0) {
      fprintf(stderr, "SeedPlusPlus: c %u/%u\n", c, num_clusters_);
    }
    double x = RandZeroToOne() * sum_dists;
    unsigned int o;
    o = BinarySearch(x, 0, num_objects_, cum_distance_to_nearest, used);
    used[o] = true;
    means_[c] = objects_[o];
    // fprintf(stderr, "c %u o %u\n", c, o);
    sum_dists = 0;
    double cum_dist = 0;
    for (unsigned int o = 0; o < num_objects_; ++o) {
      if (used[o]) {
	cum_distance_to_nearest[o] = cum_dist;
	continue;
      }
      int delta = objects_[o] - means_[c];
      int dist = delta >= 0 ? delta : -delta;
      if (dist < distance_to_nearest[o]) {
	distance_to_nearest[o] = dist;
      }
      sum_dists += distance_to_nearest[o];
      cum_dist += distance_to_nearest[o];
      cum_distance_to_nearest[o] = cum_dist;
    }
  }

  for (unsigned int c = 0; c < num_clusters_; ++c) {
    cluster_sizes_[c] = 1;
  }
  
  delete [] distance_to_nearest;
  delete [] cum_distance_to_nearest;
  delete [] used;
}

unsigned int UnivariateKMeans::Assign(void) {
  worst_dist_ = 0;
  unsigned int num_changed = 0;
  for (unsigned int o = 0; o < num_objects_; ++o) {
    int val = objects_[o];
    unsigned int best_c = 0;
    int dist0 = val - means_[0];
    if (dist0 < 0) dist0 = -dist0;
    int min_dist = dist0;
    for (unsigned int c = 1; c < num_clusters_; ++c) {
      int dist = val - means_[c];
      if (dist < 0) dist = -dist;
      if (dist < min_dist) {
	min_dist = dist;
	best_c = c;
      }
    }
    if (min_dist > worst_dist_) worst_dist_ = min_dist;
    if (best_c != assignments_[o]) ++num_changed;
    assignments_[o] = best_c;
  }

  return num_changed;
}

void UnivariateKMeans::Update(void) {
  long long int *sums = new long long int[num_clusters_];
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    sums[c] = 0;
    cluster_sizes_[c] = 0;
  }
  for (unsigned int o = 0; o < num_objects_; ++o) {
    unsigned int c = assignments_[o];
    // During initialization we will assign some objects to cluster kMaxUInt
    // meaning they are unassigned
    if (c == kMaxUInt) continue;
    sums[c] += objects_[o];
    ++cluster_sizes_[c];
  }
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    if (cluster_sizes_[c] > 0) {
      means_[c] = ((double)sums[c]) / (double)cluster_sizes_[c];
    } else {
      means_[c] = 0;
    }
  }
  delete [] sums;
}

void UnivariateKMeans::EliminateEmpty(void) {
  unsigned int *mapping = new unsigned int[num_clusters_];
  for (unsigned int j = 0; j < num_clusters_; ++j) {
    mapping[j] = kMaxUInt;
  }
  unsigned int i = 0;
  for (unsigned int j = 0; j < num_clusters_; ++j) {
    if (cluster_sizes_[j] > 0) {
      mapping[j] = i;
      cluster_sizes_[i] = cluster_sizes_[j];
      means_[i] = means_[j];
      ++i;
    }
  }
  num_clusters_ = i;
  for (unsigned int o = 0; o < num_objects_; ++o) {
    unsigned int old_c = assignments_[o];
    assignments_[o] = mapping[old_c];
  }
  delete [] mapping;
}

void UnivariateKMeans::Cluster(unsigned int num_clusters,
			       unsigned int num_its) {
  if (means_) delete [] means_;
  if (cluster_sizes_) delete [] cluster_sizes_;
  worst_dist_ = 0;

  if (num_clusters >= num_objects_) {
    // Assign every object to its own cluster
    num_clusters_ = num_objects_;
    means_ = new double[num_clusters_];
    cluster_sizes_ = new unsigned int[num_clusters_];
    for (unsigned int o = 0; o < num_objects_; ++o) {
      assignments_[o] = o;
      means_[o] = o;
      cluster_sizes_[o] = 1;
    }
    return;
  }
  for (unsigned int o = 0; o < num_objects_; ++o) assignments_[o] = kMaxUInt;
  num_clusters_ = num_clusters;
  means_ = new double[num_clusters_];
  cluster_sizes_ = new unsigned int[num_clusters_];
  // means_ and cluster_sizes_ initialized in SeedPlusPlus()
  // assignments_ will not be initialized until the first call to Assign()
  // below
  SeedPlusPlus();

  unsigned int it = 0;
  while (true) {
    unsigned int num_changed = Assign();
    // fprintf(stderr, "It %i num_changed %i\n", it, num_changed);
    Update();

    if (num_changed == 0 || it == num_its - 1) break;

    ++it;
  }
  EliminateEmpty();
}
