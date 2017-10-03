// Would like for code to be as generic as possible.  But different objects
// being clustered will have different types of features.
// Have to take the square root or the triangle equality based test will not
// work properly.

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "constants.h"
#include "kmeans.h"
#include "rand.h"
#include "sorting.h"

static unsigned int g_it = 0;

class KMeansThread {
public:
  KMeansThread(unsigned int num_objects, unsigned int num_clusters,
	       float **objects, unsigned int dim, double neighbor_thresh,
	       unsigned int *cluster_sizes, float **means,
	       unsigned int *assignments, unsigned char **nearest_centroids,
	       unsigned char **neighbor_ptrs,
	       vector< pair<float, unsigned int> > *neighbor_vectors,
	       unsigned int thread_index, unsigned int num_threads);
  ~KMeansThread(void) {}
  void Assign(void);
  void ComputeIntraCentroidDistances(void);
  void SortNeighbors();
  void RunAssign(void);
  void RunIntra(void);
  void RunSort(void);
  void Join(void);
  unsigned int NumChanged(void) const {return num_changed_;}
  double SumDists(void) const {return sum_dists_;}
private:
  unsigned int ExhaustiveNearest(unsigned int o, float *obj,
				 unsigned int guess_c, double guess_min_dist,
				 double *ret_min_dist);
  unsigned int Nearest(unsigned int o, float *obj, double *ret_min_dist);

  unsigned int num_objects_;
  unsigned int num_clusters_;
  float **objects_;
  unsigned int dim_;
  double neighbor_thresh_;
  unsigned int *cluster_sizes_;
  float **means_;
  unsigned int *assignments_;
  unsigned char **nearest_centroids_;
  unsigned char **neighbor_ptrs_;
  vector< pair<float, unsigned int> > *neighbor_vectors_;
  unsigned int thread_index_;
  unsigned int num_threads_;
  unsigned int num_changed_;
  double sum_dists_;
  unsigned long long int exhaustive_count_;
  unsigned long long int abbreviated_count_;
  unsigned long long int dist_count_;
  pthread_t pthread_id_;
};

KMeansThread::KMeansThread(unsigned int num_objects,
			   unsigned int num_clusters, float **objects,
			   unsigned int dim, double neighbor_thresh,
			   unsigned int *cluster_sizes,
			   float **means, unsigned int *assignments,
			   unsigned char **nearest_centroids,
			   unsigned char **neighbor_ptrs,
			   vector< pair<float, unsigned int> > *
			   neighbor_vectors,
			   unsigned int thread_index,
			   unsigned int num_threads) {
  num_objects_ = num_objects;
  num_clusters_ = num_clusters;
  objects_ = objects;
  dim_ = dim;
  neighbor_thresh_ = neighbor_thresh;
  cluster_sizes_ = cluster_sizes;
  means_ = means;
  assignments_ = assignments;
  nearest_centroids_ = nearest_centroids;
  neighbor_ptrs_ = neighbor_ptrs;
  neighbor_vectors_ = neighbor_vectors;
  thread_index_ = thread_index;
  num_threads_ = num_threads;
  num_changed_ = 0;
}

#if 0
// Could add early termination when dist exceeds min dist
unsigned int KMeansThread::Nearest(unsigned int o, float *obj,
				   double *ret_min_dist) {
  // Initialize best_c to the current assignment.  This will make the triangle
  // inequality based optimization work better.
  unsigned int best_c = assignments_[o];
  if (best_c == kMaxUInt) {
    // Arbitrarily set best_c to first cluster with non-zero size
    unsigned int c;
    for (c = 0; c < num_clusters_; ++c) {
      if (cluster_sizes_[c] > 0) {
	best_c = c;
	break;
      }
    }
    if (c == num_clusters_) {
      fprintf(stderr, "No clusters with non-zero size?!?\n");
      exit(-1);
    }
  }
  float orig_min_dist = 0;
  for (unsigned int d = 0; d < dim_; ++d) {
    float ov = obj[d];
    float cm = means_[best_c][d];
    float dim_delta = ov - cm;
    orig_min_dist += dim_delta * dim_delta;
  }
  orig_min_dist = sqrt(orig_min_dist);
  float min_dist = orig_min_dist;
  unsigned char *neighbors = nearest_centroids_[best_c];
  while (true) {
    unsigned int c = *(unsigned int *)neighbors;
    if (c == kMaxUInt) break;
    if (c == best_c) continue;
    if (cluster_sizes_[c] == 0) continue;
    neighbors += sizeof(unsigned int);
    float centroid_dist = *(float *)neighbors;
    neighbors += sizeof(float);
    if (centroid_dist >= 2 * orig_min_dist) {
      continue;
    }

    float *cluster_means = means_[c];
    float dist_sq = 0;
    for (unsigned int d = 0; d < dim_; ++d) {
      float ov = obj[d];
      float cm = cluster_means[d];
      float dim_delta = ov - cm;
      dist_sq += dim_delta * dim_delta;
    }
    float dist = sqrt(dist_sq);
    if (dist < min_dist) {
      best_c = c;
      min_dist = dist;
    }
  }
  *ret_min_dist = min_dist;
  return best_c;
}
#endif

unsigned int KMeansThread::ExhaustiveNearest(unsigned int o, float *obj,
					     unsigned int guess_c,
					     double guess_min_dist,
					     double *ret_min_dist) {
  float min_dist = guess_min_dist;
  unsigned int best_c = guess_c;
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    if (c == best_c) continue;
    if (cluster_sizes_[c] == 0) continue;

    float *cluster_means = means_[c];
    float dist_sq = 0;
    for (unsigned int d = 0; d < dim_; ++d) {
      float ov = obj[d];
      float cm = cluster_means[d];
      float dim_delta = ov - cm;
      dist_sq += dim_delta * dim_delta;
    }
    float dist = sqrt(dist_sq);
    ++dist_count_;
    if (dist < min_dist) {
      best_c = c;
      min_dist = dist;
    } else if (dist == min_dist) {
      // In case of tie, choose the lower numbered cluster
      if (c < best_c) best_c = c;
    }
  }
  *ret_min_dist = min_dist;
  return best_c;
}

// Finds the nearest centroid to the given object o.  We have the current
// best assignment for o.  Call it c.  (If this is the first iteration,
// then choose a cluster c arbitrarily.)  We will start by only considering
// the nearest neighbors of c.  Let orig_dist be D(o, c).  Maintain the
// closest cluster found which might be the original cluster or might be
// one of the neighbors.  If we ever get to a neighboring cluster c' such
// that D(o, c') >= 2 * orig_dist then we can quit (triangle inequality).
// If we get to the end of the neighbors list then there are two possibilities
// 1) we found a new best candidate; or 2) we did not.  In case (2) we have
// no choice but to do an exhaustive search through all clusters which we do
// with ExhaustiveNearest().  Hopefully this doesn't happen too often.  In
// case (1) we can instead repeat the process we just followed, this time
// using the neighbors list of the new best candidate.
unsigned int KMeansThread::Nearest(unsigned int o, float *obj,
				   double *ret_min_dist) {
  // Initialize orig_best_c to the current assignment.  This will make the
  // triangle inequality based optimization work better.
  unsigned int orig_best_c = assignments_[o];
  if (orig_best_c == kMaxUInt) {
    // Arbitrarily set orig_best_c to first cluster with non-zero size
    unsigned int c;
    for (c = 0; c < num_clusters_; ++c) {
      if (cluster_sizes_[c] > 0) {
	orig_best_c = c;
	break;
      }
    }
    if (c == num_clusters_) {
      fprintf(stderr, "No clusters with non-zero size?!?\n");
      exit(-1);
    }
  }
  float orig_dist = 0;
  for (unsigned int d = 0; d < dim_; ++d) {
    float ov = obj[d];
    float cm = means_[orig_best_c][d];
    float dim_delta = ov - cm;
    orig_dist += dim_delta * dim_delta;
  }
  orig_dist = sqrt(orig_dist);
  ++dist_count_;
  // For testing purposes, if we can just call ExhaustiveNearest() here if
  // we suspect a bug in the optimized code below.
  if (neighbor_vectors_ == NULL) {
    return ExhaustiveNearest(o, obj, orig_best_c, orig_dist, ret_min_dist);
  }
  float min_dist = orig_dist;
  unsigned int best_c = orig_best_c;
  while (true) {
    const vector< pair<float, unsigned int> > &v = neighbor_vectors_[best_c];
    unsigned int num = v.size();
    for (unsigned int i = 0; i < num; ++i) {
      float intra_dist = v[i].first;
      unsigned int c = v[i].second;
      if (cluster_sizes_[c] == 0) continue;
      // Has to be orig_dist, not min_dist.
      if (intra_dist >= 2 * orig_dist) {
	// We sorted neighbors by distance so we can skip all other clusters
	++abbreviated_count_;
	*ret_min_dist = min_dist;
	return best_c;
      }

      float *cluster_means = means_[c];
      float dist_sq = 0;
      for (unsigned int d = 0; d < dim_; ++d) {
	float ov = obj[d];
	float cm = cluster_means[d];
	float dim_delta = ov - cm;
	dist_sq += dim_delta * dim_delta;
      }
      float dist = sqrt(dist_sq);
      ++dist_count_;
      if (dist < min_dist) {
	best_c = c;
	min_dist = dist;
      } else if (dist == min_dist) {
	// In case of tie, choose the lower numbered cluster
	if (c < best_c) best_c = c;
      }
    }
    if (best_c == orig_best_c) {
      // We got to the end of the neighbors list and we a) could not prove
      // that we had the best cluster, and b) didn't find a better candidate
      // best cluster.  All we can do now is an exhaustive search of all
      // the clusters.
      ++exhaustive_count_;
      return ExhaustiveNearest(o, obj, best_c, min_dist, ret_min_dist);
    } else {
      // We got to the end of the neighbors list and we could not prove
      // that we had the best cluster, *but* we did find a better candidate
      // cluster.  So we can now try to search the neighbors of this new
      // better candidate.
      orig_best_c = best_c;
      orig_dist = min_dist;
      continue;
    }
  }

  fprintf(stderr, "Should never get here\n");
  exit(-1);
}

void KMeansThread::Assign(void) {
  abbreviated_count_ = 0ULL;
  exhaustive_count_ = 0ULL;
  dist_count_ = 0ULL;
  num_changed_ = 0;
  double dist;
  sum_dists_ = 0;
  for (unsigned int o = thread_index_; o < num_objects_; o += num_threads_) {
    if (g_it == 0 && thread_index_ == 0 && (o / num_threads_) % 10000 == 0) {
      fprintf(stderr, "It %u o %u/%u\n", g_it, o, num_objects_);
    }
    float *obj = objects_[o];
    unsigned int nearest = Nearest(o, obj, &dist);
    sum_dists_ += dist;
    if (nearest != assignments_[o]) ++num_changed_;
    assignments_[o] = nearest;
  }
  if (thread_index_ == 0) {
    fprintf(stderr, "Abbreviated: %.2f%% (%llu/%llu)\n",
	    100.0 * abbreviated_count_ /
	    (double)(abbreviated_count_ + exhaustive_count_),
	    num_threads_ * abbreviated_count_,
	    num_threads_ * (abbreviated_count_ + exhaustive_count_));
    fprintf(stderr, "Dist count: %llu\n", dist_count_);
    // Divide by num_threads because we are reporting numbers for one thread
    unsigned long long int naive_dist_count =
      ((unsigned long long int)num_objects_) *
      ((unsigned long long int)num_clusters_) / num_threads_;
    fprintf(stderr, "Dist pct: %.2f%%\n",
	    100.0 * dist_count_ / (double)naive_dist_count);
  }
}

#if 0
// Can I parallelize SeedPlusPlus()?  What if I select N clusters
// independently each time?
void KMeansThread::SeedPlusPlus(void) {
}
#endif

void KMeansThread::ComputeIntraCentroidDistances(void) {
  // First loop puts c1 on c2's list for c1 < c2
  for (unsigned int c1 = 0; c1 < num_clusters_ - 1; ++c1) {
    if (cluster_sizes_[c1] == 0) continue;
    float *cluster_means1 = means_[c1];
    for (unsigned int c2 = c1 + 1; c2 < num_clusters_; ++c2) {
      if (cluster_sizes_[c2] == 0) continue;
      // Only do work that is mine
      if (c2 % num_threads_ != thread_index_) continue;
      float *cluster_means2 = means_[c2];
      float dist_sq = 0;
      for (unsigned int d = 0; d < dim_; ++d) {
	float cm1 = cluster_means1[d];
	float cm2 = cluster_means2[d];
	float delta = cm2 - cm1;
	dist_sq += delta * delta;
      }
      float dist = sqrt(dist_sq);
      if (dist >= neighbor_thresh_) continue;
      neighbor_vectors_[c2].push_back(make_pair(dist, c1));
    }
  }
}

void KMeansThread::SortNeighbors(void) {
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    // Only do work that is mine
    if (c % num_threads_ != thread_index_) continue;
    vector< pair<float, unsigned int> > *v = &neighbor_vectors_[c];
    sort(v->begin(), v->end(), g_pfui_lower_compare);
  }
}

static void *thread_run_assign(void *v_t) {
  KMeansThread *t = (KMeansThread *)v_t;
  t->Assign();
  return NULL;
}

void KMeansThread::RunAssign(void) {
  pthread_create(&pthread_id_, NULL, thread_run_assign, this);
}

static void *thread_run_intra(void *v_t) {
  KMeansThread *t = (KMeansThread *)v_t;
  t->ComputeIntraCentroidDistances();
  return NULL;
}

void KMeansThread::RunIntra(void) {
  pthread_create(&pthread_id_, NULL, thread_run_intra, this);
}

static void *thread_run_sort(void *v_t) {
  KMeansThread *t = (KMeansThread *)v_t;
  t->SortNeighbors();
  return NULL;
}

void KMeansThread::RunSort(void) {
  pthread_create(&pthread_id_, NULL, thread_run_sort, this);
}

void KMeansThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

unsigned int KMeans::BinarySearch(double r, unsigned int begin,
				  unsigned int end,
				  double *cum_sq_distance_to_nearest,
				  bool *used) {
  if (end == begin + 1) {
    // Is it possible that we will select a used object?
    if (used[begin]) {
      fprintf(stderr, "Ended up with used object?!?\n");
      fprintf(stderr, "r %f cum_sq_distance_to_nearest %f %f %f %f\n", r,
	      cum_sq_distance_to_nearest[begin - 2],
	      cum_sq_distance_to_nearest[begin - 1],
	      cum_sq_distance_to_nearest[begin],
	      cum_sq_distance_to_nearest[end]);
      exit(-1);
    }
    return begin;
  } else {
    // The median may be an already used object, but cum_sq_distance_to_nearest
    // is set appropriately.
    unsigned int median = (end + begin) / 2;
    double median_val = cum_sq_distance_to_nearest[median];
    if (r < median_val) {
      if (median == 0) return 0;
      // Find the previous object that is not used
      int prev;
      for (prev = median - 1; prev >= 0; --prev) {
	if (! used[prev]) {
	  if (r >= cum_sq_distance_to_nearest[prev]) {
	    return median;
	  } else {
	    return BinarySearch(r, begin, median, cum_sq_distance_to_nearest,
				used);
	  }
	}
      }
      // If we get here, median is the first unused object
      return median;
    } else if (r > median_val) {
      return BinarySearch(r, median + 1, end, cum_sq_distance_to_nearest, used);
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
// Should maintain cum_sq_distance_to_nearest, do binary search on it
// for lookup.  Still need O(n) update to sq_distance_to_nearest though.
// Can we update sq_distance_to_nearest only occasionally?
void KMeans::SeedPlusPlus(void) {
  bool *used = new bool[num_objects_];
  for (unsigned int o = 0; o < num_objects_; ++o) used[o] = false;
  // For the first centroid, choose one of the input objects at random
  unsigned int o = RandBetween(0, num_objects_ - 1);
  used[o] = true;
  for (unsigned int f = 0; f < dim_; ++f) {
    means_[0][f] = objects_[o][f];
  }
  double *sq_distance_to_nearest = new double[num_objects_];
  double *cum_sq_distance_to_nearest = new double[num_objects_];
  double sum_min_sq_dist = 0;
  double cum_sq_dist = 0;
  for (unsigned int o = 0; o < num_objects_; ++o) {
    if (used[o]) {
      cum_sq_distance_to_nearest[o] = cum_sq_dist;
      continue;
    }
    double sq_dist = 0;
    float *obj = objects_[o];
    for (unsigned int d = 0; d < dim_; ++d) {
      double ov = obj[d];
      double cm = means_[0][d];
      float dim_delta = ov - cm;
      sq_dist += dim_delta * dim_delta;
    }
    sq_distance_to_nearest[o] = sq_dist;
    cum_sq_dist += sq_dist;
    cum_sq_distance_to_nearest[o] = cum_sq_dist;
    sum_min_sq_dist += sq_dist;
  }
  for (unsigned int c = 1; c < num_clusters_; ++c) {
    if (c % 1000 == 0) {
      fprintf(stderr, "SeedPlusPlus: c %u/%u\n", c, num_clusters_);
    }
    double x = RandZeroToOne() * sum_min_sq_dist;
    unsigned int o;
#if 1
    o = BinarySearch(x, 0, num_objects_, cum_sq_distance_to_nearest, used);
#else
    double cum = 0;
    for (o = 0; o < num_objects_; ++o) {
      if (used[o]) continue;
      cum += sq_distance_to_nearest[o];
      if (x <= cum) break;
    }
#endif
    used[o] = true;
    // Helps with old version of search
    sq_distance_to_nearest[o] = 0;
    for (unsigned int f = 0; f < dim_; ++f) {
      means_[c][f] = objects_[o][f];
    }
    sum_min_sq_dist = 0;
    double cum_sq_dist = 0;
    for (unsigned int o = 0; o < num_objects_; ++o) {
      if (used[o]) {
	cum_sq_distance_to_nearest[o] = cum_sq_dist;
	continue;
      }
      float *obj = objects_[o];
      double sq_dist = 0;
      for (unsigned int d = 0; d < dim_; ++d) {
	double ov = obj[d];
	double cm = means_[c][d];
	float dim_delta = ov - cm;
	sq_dist += dim_delta * dim_delta;
      }
      if (sq_dist < sq_distance_to_nearest[o]) {
	sq_distance_to_nearest[o] = sq_dist;
      }
      sum_min_sq_dist += sq_distance_to_nearest[o];
      cum_sq_dist += sq_distance_to_nearest[o];
      cum_sq_distance_to_nearest[o] = cum_sq_dist;
    }
  }
  delete [] sq_distance_to_nearest;
  delete [] cum_sq_distance_to_nearest;
  delete [] used;
}

// Choose one item at random to serve as the seed of each cluster
void KMeans::Seed1(void) {
  bool *used = new bool[num_objects_];
  for (unsigned int o = 0; o < num_objects_; ++o) used[o] = false;
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    unsigned int o;
    do {
      o = RandBetween(0, num_objects_ - 1);
    } while (used[o]);
    used[o] = true;
    for (unsigned int f = 0; f < dim_; ++f) {
      means_[c][f] = objects_[o][f];
    }
  }
  delete [] used;
}

// Seed each cluster with the average of 10 randomly selected points
// I saw a lot of empty clusters after seeding this way.  Switching to
// Seed1() method.
void KMeans::Seed2(void) {
  double *sums = new double[dim_];
  unsigned int num_sample = 10;
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    for (unsigned int f = 0; f < dim_; ++f) sums[f] = 0;
    for (unsigned int i = 0; i < num_sample; ++i) {
      unsigned int o = RandBetween(0, num_objects_ - 1);
      for (unsigned int f = 0; f < dim_; ++f) {
	sums[f] += objects_[o][f];
      }
    }
    for (unsigned int f = 0; f < dim_; ++f) {
      means_[c][f] = sums[f] / num_sample;
    }
  }
  delete [] sums;
}

// Should I assume dups have been removed?
void KMeans::SingleObjectClusters(unsigned int num_clusters, unsigned int dim,
				  unsigned int num_objects, float **objects) {
  num_clusters_ = num_objects;
  dim_ = dim;
  num_objects_ = num_objects;
  objects_ = objects;
  cluster_sizes_ = new unsigned int[num_clusters_];
  means_ = new float *[num_clusters_];
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    means_[c] = new float[dim];
  }
  assignments_ = new unsigned int[num_objects_];
  for (unsigned int o = 0; o < num_objects_; ++o) {
    unsigned int c = o;
    assignments_[o] = c;
    for (unsigned int f = 0; f < dim_; ++f) {
      means_[c][f] = objects[o][f];
    }
    cluster_sizes_[c] = 1;
  }
  nearest_centroids_ = NULL;
  threads_ = NULL;
  num_threads_ = 0;
}

KMeans::KMeans(unsigned int num_clusters, unsigned int dim,
	       unsigned int num_objects, float **objects,
	       double neighbor_thresh, unsigned int num_threads) {
  nearest_centroids_ = NULL;
  neighbor_ptrs_ = NULL;
  neighbor_vectors_ = NULL;
  cluster_sizes_ = NULL;
  means_ = NULL;
  assignments_ = NULL;
  threads_ = NULL;
  if (num_clusters >= num_objects) {
    fprintf(stderr, "Assigning every object to its own cluster\n");
    SingleObjectClusters(num_clusters, dim, num_objects, objects);
    return;
  }
  num_clusters_ = num_clusters;
  fprintf(stderr, "%i objects\n", num_objects);
  fprintf(stderr, "Using target num clusters: %i\n", num_clusters_);
  dim_ = dim;
  num_objects_ = num_objects;
  objects_ = objects;
  neighbor_thresh_ = neighbor_thresh;
  cluster_sizes_ = new unsigned int[num_clusters_];
  means_ = new float *[num_clusters_];
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    means_[c] = new float[dim];
  }
  assignments_ = new unsigned int[num_objects_];
  for (unsigned int o = 0; o < num_objects_; ++o) assignments_[o] = kMaxUInt;

  intra_time_ = 0;
  assign_time_ = 0;

  // SeedPlusPlus() is pretty slow.  For now don't use when more than 10,000
  // clusters and more than 1m objects.
  if (num_clusters >= 1000 && num_objects >= 1000000) {
    fprintf(stderr, "Calling Seed1\n");
    Seed1();
    fprintf(stderr, "Back from Seed1\n");
  } else {
    fprintf(stderr, "Calling SeedPlusPlus\n");
    SeedPlusPlus();
    fprintf(stderr, "Back from SeedPlusPlus\n");
  }

  // This is a hack.  Nearest() ignores clusters with zero-size.  But for the
  // initial assignment, we don't want this.  Cluster sizes will get set
  // properly in Update().
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    cluster_sizes_[c] = 1;
  }

  // If neighbor_thresh_ is zero, don't compute neighbors lists.
  if (neighbor_thresh_ > 0) {
#if 0
    // We used to use nearest_centroids_ data structure before having neighbors
    // lists (I think).
    nearest_centroids_ = new unsigned char *[num_clusters_];
    unsigned int len =
      (sizeof(float) + sizeof(unsigned int)) * kMaxNeighbors +
      sizeof(unsigned int);
    for (unsigned int c = 0; c < num_clusters_; ++c) {
      nearest_centroids_[c] = new unsigned char[len];
    }
    neighbor_ptrs_ = new unsigned char *[num_clusters_];
#endif
    neighbor_vectors_ = new vector< pair<float, unsigned int> >[num_clusters_];
  } else {
    neighbor_vectors_ = NULL;
  }

  num_threads_ = num_threads;
  threads_ = new KMeansThread *[num_threads_];
  for (unsigned int t = 0; t < num_threads_; ++t) {
    threads_[t] = new KMeansThread(num_objects_, num_clusters_, objects_,
				   dim_, neighbor_thresh_, cluster_sizes_,
				   means_, assignments_, nearest_centroids_,
				   neighbor_ptrs_, neighbor_vectors_,
				   t, num_threads_);
  }

  // Normally we call this at the end of each iteration.  Call it once now
  // before the first iteration to speed up the first call to Assign().
  if (neighbor_thresh_ > 0) {
    fprintf(stderr, "Calling initial ComputeIntraCentroidDistances()\n");
    ComputeIntraCentroidDistances();
    fprintf(stderr,
	    "Back from initial call to ComputeIntraCentroidDistances()\n");
  }
}

// Assume caller owns objects
KMeans::~KMeans(void) {
  if (threads_) {
    for (unsigned int t = 0; t < num_threads_; ++t) {
      delete threads_[t];
    }
    delete [] threads_;
  }
  if (nearest_centroids_) {
    for (unsigned int c = 0; c < num_clusters_; ++c) {
      delete [] nearest_centroids_[c];
    }
    delete [] nearest_centroids_;
  }
  delete [] neighbor_ptrs_;
  delete [] neighbor_vectors_;
  delete [] cluster_sizes_;
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    delete [] means_[c];
  }
  delete [] means_;
  delete [] assignments_;
}

// Assumes cluster means are up-to-date
void KMeans::ComputeIntraCentroidDistances(void) {
  time_t start_t = time(NULL);

  for (unsigned int c = 0; c < num_clusters_; ++c) {
    neighbor_vectors_[c].clear();
  }

  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->RunIntra();
  }
  // Execute thread 0 in main execution thread
  threads_[0]->ComputeIntraCentroidDistances();
  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->Join();
  }

  fprintf(stderr, "Starting second loop\n");
  for (unsigned int c1 = 1; c1 < num_clusters_; ++c1) {
    if (cluster_sizes_[c1] == 0) continue;
    vector< pair<float, unsigned int> > *v = &neighbor_vectors_[c1];
    unsigned int num = v->size();
    for (unsigned int i = 0; i < num; ++i) {
      float dist = (*v)[i].first;
      unsigned int c0 = (*v)[i].second;
      neighbor_vectors_[c0].push_back(make_pair(dist, c1));
    }
  }

  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->RunSort();
  }
  threads_[0]->SortNeighbors();
  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->Join();
  }

  unsigned int sum_lens = 0;
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    // printf("%i\n", (int)neighbor_vectors_[c].size());
    sum_lens += neighbor_vectors_[c].size();
  }
  fprintf(stderr, "Avg neighbor vector length: %.1f\n",
	  sum_lens / (double)num_clusters_);
  // exit(0);

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  intra_time_ += diff_sec;
  fprintf(stderr, "Cum intra time: %f\n", intra_time_);
}

unsigned int KMeans::Assign(double *avg_dist) {
  time_t start_t = time(NULL);

  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->RunAssign();
  }
  // Execute thread 0 in main execution thread
  threads_[0]->Assign();
  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->Join();
  }
  unsigned int num_changed = 0;
  double sum_dists = 0;
  for (unsigned int i = 0; i < num_threads_; ++i) {
    num_changed += threads_[i]->NumChanged();
    sum_dists += threads_[i]->SumDists();
  }
  *avg_dist = sum_dists / num_objects_;

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  assign_time_ += diff_sec;
  fprintf(stderr, "Cum assign time: %f\n", assign_time_);

  return num_changed;
}

void KMeans::Update(void) {
  float **sums = new float *[num_clusters_];
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    sums[c] = new float[dim_];
    for (unsigned int d = 0; d < dim_; ++d) {
      sums[c][d] = 0;
    }
    cluster_sizes_[c] = 0;
  }
  for (unsigned int o = 0; o < num_objects_; ++o) {
    unsigned int c = assignments_[o];
    // During initialization we will assign some objects to cluster kMaxUInt
    // meaning they are unassigned
    if (c == kMaxUInt) continue;
    float *obj = objects_[o];
    for (unsigned int d = 0; d < dim_; ++d) {
      sums[c][d] += obj[d];
    }
    ++cluster_sizes_[c];
  }
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    if (cluster_sizes_[c] > 0) {
      for (unsigned int d = 0; d < dim_; ++d) {
	means_[c][d] = sums[c][d] / cluster_sizes_[c];
      }
    } else {
      for (unsigned int d = 0; d < dim_; ++d) means_[c][d] = 0;
    }
    delete [] sums[c];
  }
  delete [] sums;
}

void KMeans::EliminateEmpty(void) {
  unsigned int *mapping = new unsigned int[num_clusters_];
  for (unsigned int j = 0; j < num_clusters_; ++j) {
    mapping[j] = kMaxUInt;
  }
  unsigned int i = 0;
  for (unsigned int j = 0; j < num_clusters_; ++j) {
    if (cluster_sizes_[j] > 0) {
      mapping[j] = i;
      cluster_sizes_[i] = cluster_sizes_[j];
      for (unsigned int d = 0; d < dim_; ++d) {
	means_[i][d] = means_[j][d];
      }
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

void KMeans::Cluster(unsigned int num_its) {
  if (num_objects_ == num_clusters_) {
    // We already did the "clustering" in the constructor
    return;
  }
  unsigned int it = 0;
  while (true) {
    g_it = it;
    double avg_dist;
    unsigned int num_changed = Assign(&avg_dist);
    fprintf(stderr, "It %i num_changed %i avg dist %f\n", it, num_changed,
	    avg_dist);

    Update();

    // Break out if num_changed <= 2.  Otherwise we can get stuck there for
    // many many iterations with no improvment.
    if (num_changed <= 2 || it == num_its - 1) {
      break;
    }

    if (neighbor_thresh_ > 0) {
      ComputeIntraCentroidDistances();
    }

    ++it;
  }
  EliminateEmpty();
}
