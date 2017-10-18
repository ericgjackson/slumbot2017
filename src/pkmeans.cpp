// Would like for code to be as generic as possible.  But different objects
// being clustered will have different types of features.
// Have to take the square root or the triangle equality based test will not
// work properly.
//
// Should I have fixed number of neighbors or max distance for neighbors?
// The appropriate max distance varies a lot depending on the problem and
// perhaps can't be easily guessed.
//
// If I have a max distance then if c1 is a neighbor c2, c2 is also a
// neighbor of c1.  We can take advantage of that to cut neighbor calculation
// time by half.

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "constants.h"
#include "pkmeans.h"
#include "rand.h"
#include "sorting.h"

static unsigned int g_it = 0;

class PKMeansThread {
public:
  PKMeansThread(unsigned int num_objects, unsigned int num_clusters,
		float **objects, unsigned int dim, double neighbor_thresh,
		unsigned int *cluster_sizes, float **means,
		unsigned int num_pivots, float **pivot_means,
		unsigned int *assignments, unsigned char **nearest_centroids,
		float *centroid_pivot_distances, unsigned int *best_pivots,
		vector< pair<float, unsigned int> > *neighbor_vectors,
		unsigned int max_neighbor_vector_length,
		unsigned int thread_index, unsigned int num_threads);
  ~PKMeansThread(void) {}
  void Assign(void);
  void ComputeCentroidPivotDistances(void);
  void ComputeIntraCentroidDistances(void);
  void SortNeighbors();
  void RunAssign(void);
  void RunPivot(void);
  void RunIntra(void);
  void RunSort(void);
  void Join(void);
  unsigned int NumChanged(void) const {return num_changed_;}
  double SumDists(void) const {return sum_dists_;}
private:
  unsigned int ExhaustiveNearest(unsigned int o, float *obj,
				 unsigned int guess_c, double guess_min_dist,
				 double *ret_min_dist);
  unsigned int GetInitialGuess(unsigned int o);
  unsigned int Nearest(unsigned int o, float *obj, double *ret_min_dist);

  unsigned int num_objects_;
  unsigned int num_clusters_;
  float **objects_;
  unsigned int dim_;
  double neighbor_thresh_;
  unsigned int *cluster_sizes_;
  float **means_;
  unsigned int *assignments_;
  unsigned int num_pivots_;
  float **pivot_means_;
  float *centroid_pivot_distances_;
  unsigned int *best_pivots_;
  unsigned char **nearest_centroids_;
  vector< pair<float, unsigned int> > *neighbor_vectors_;
  // Unused currently
  unsigned int max_neighbor_vector_length_;
  unsigned int thread_index_;
  unsigned int num_threads_;
  unsigned int num_changed_;
  double sum_dists_;
  unsigned long long int exhaustive_count_;
  unsigned long long int abbreviated_count_;
  unsigned long long int dist_count_;
  pthread_t pthread_id_;
};

PKMeansThread::PKMeansThread(unsigned int num_objects,
			     unsigned int num_clusters, float **objects,
			     unsigned int dim, double neighbor_thresh,
			     unsigned int *cluster_sizes,
			     float **means, unsigned int num_pivots,
			     float **pivot_means,
			     unsigned int *assignments,
			     unsigned char **nearest_centroids,
			     float *centroid_pivot_distances,
			     unsigned int *best_pivots,
			     vector< pair<float, unsigned int> > *
			     neighbor_vectors,
			     unsigned int max_neighbor_vector_length,
			     unsigned int thread_index,
			     unsigned int num_threads) {
  num_objects_ = num_objects;
  num_clusters_ = num_clusters;
  objects_ = objects;
  dim_ = dim;
  neighbor_thresh_ = neighbor_thresh;
  cluster_sizes_ = cluster_sizes;
  means_ = means;
  num_pivots_ = num_pivots;
  pivot_means_ = pivot_means;
  assignments_ = assignments;
  nearest_centroids_ = nearest_centroids;
  centroid_pivot_distances_ = centroid_pivot_distances;
  best_pivots_ = best_pivots;
  neighbor_vectors_ = neighbor_vectors;
  max_neighbor_vector_length_ = max_neighbor_vector_length;
  thread_index_ = thread_index;
  num_threads_ = num_threads;
  num_changed_ = 0;
}

unsigned int PKMeansThread::ExhaustiveNearest(unsigned int o, float *obj,
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

// Return an initial guess as to which cluster is closest to the given
// object.  This is the cluster that was assigned on the last iteration.
// On the first iteration we arbitrarily pick one cluster.
unsigned int PKMeansThread::GetInitialGuess(unsigned int o) {
  // Initialize orig_best_c to the current assignment.  This will make the
  // triangle inequality based optimization work better.
  unsigned int initial_guess = assignments_[o];
  if (initial_guess == kMaxUInt) {
    // Arbitrarily set initial_guess to first cluster with non-zero size
    unsigned int c;
    for (c = 0; c < num_clusters_; ++c) {
      if (cluster_sizes_[c] > 0) {
	initial_guess = c;
	break;
      }
    }
    if (c == num_clusters_) {
      fprintf(stderr, "No clusters with non-zero size?!?\n");
      exit(-1);
    }
  }
  return initial_guess;
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
unsigned int PKMeansThread::Nearest(unsigned int o, float *obj,
				    double *ret_min_dist) {
  unsigned int initial_guess = GetInitialGuess(o);
  float initial_dist = 0;
  for (unsigned int d = 0; d < dim_; ++d) {
    float ov = obj[d];
    float cm = means_[initial_guess][d];
    float dim_delta = ov - cm;
    initial_dist += dim_delta * dim_delta;
  }
  initial_dist = sqrt(initial_dist);
  ++dist_count_;
  // For testing purposes, if we can just call ExhaustiveNearest() here if
  // we suspect a bug in the optimized code below.
  if (neighbor_vectors_ == NULL) {
    return ExhaustiveNearest(o, obj, initial_guess, initial_dist, ret_min_dist);
  }
  // current_guess is set before each iteration of the below while loop,
  // and not changed during that iteration.
  // Each iteration of the while loop scans a neighbors list.
  // best_c starts out as current_guess and gets updated if we find a
  // better cluster on the neighbors list.  Likewise current_dist and
  // min_dist.
  unsigned int current_guess = initial_guess;
  float current_dist = initial_dist;
  unsigned int best_c = initial_guess;
  float min_dist = initial_dist;
  while (true) {
    const vector< pair<float, unsigned int> > &v = neighbor_vectors_[best_c];
    unsigned int num = v.size();
    for (unsigned int i = 0; i < num; ++i) {
      float intra_dist = v[i].first;
      unsigned int c = v[i].second;
      if (cluster_sizes_[c] == 0) continue;
      // Has to be current_dist, not min_dist.
      if (intra_dist >= 2 * current_dist) {
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
    if (best_c == current_guess) {
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
      current_guess = best_c;
      current_dist = min_dist;
      continue;
    }
  }

  fprintf(stderr, "Should never get here\n");
  exit(-1);
}

void PKMeansThread::Assign(void) {
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

void PKMeansThread::ComputeCentroidPivotDistances(void) {
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    // Only do work that is mine
    if (c % num_threads_ != thread_index_) continue;
    if (cluster_sizes_[c] == 0) continue;
    float *cluster_means = means_[c];
    unsigned int best_p = kMaxUInt;
    float best_pivot_dist = 0;
    for (unsigned int p = 0; p < num_pivots_; ++p) {
      float *pivot_means = pivot_means_[p];
      float dist_sq = 0;
      for (unsigned int d = 0; d < dim_; ++d) {
	float cm = cluster_means[d];
	float pm = pivot_means[d];
	float delta = cm - pm;
	dist_sq += delta * delta;
      }
      float dist = sqrt(dist_sq);
      centroid_pivot_distances_[c * num_pivots_ + p] = dist;
      if (best_p == kMaxUInt || dist < best_pivot_dist) {
	best_p = p;
	best_pivot_dist = dist;
      }
    }
    best_pivots_[c] = best_p;
  }
}

void PKMeansThread::ComputeIntraCentroidDistances(void) {
  unsigned long long int ic_pruned = 0, ic_not_pruned = 0;
  // First loop puts c1 on c2's list for c1 < c2
  for (unsigned int c1 = 0; c1 < num_clusters_ - 1; ++c1) {
    if (cluster_sizes_[c1] == 0) continue;
    if (g_it == 0 && thread_index_ == 0 && c1 % 10000 == 0) {
      fprintf(stderr, "ComputeIntraCentroidDistances c %u/%u "
	      "IC pruned %.2f%%\n", c1, num_clusters_,
	      100.0 * ic_pruned / (ic_pruned + ic_not_pruned));
    }
    float *cluster_means1 = means_[c1];
    unsigned int p = best_pivots_[c1];
    float c1_pivot_dist = centroid_pivot_distances_[c1 * num_pivots_ + p];
    for (unsigned int c2 = c1 + 1; c2 < num_clusters_; ++c2) {
      if (cluster_sizes_[c2] == 0) continue;
      // Only do work that is mine
      if (c2 % num_threads_ != thread_index_) continue;
      float c2_pivot_dist = centroid_pivot_distances_[c2 * num_pivots_ + p];
      // Want to know if D(c1, c2) could be less than neighbor_thresh_.
      // D(c1, c2) >= |c1_pivot_dist - c2_pivot_dist|
      if (fabs(c1_pivot_dist - c2_pivot_dist) >= neighbor_thresh_) {
	++ic_pruned;
	continue;
      }
      ++ic_not_pruned;
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
  if (thread_index_ == 0) {
    fprintf(stderr, "IC pruned: %.2f%%\n",
	    100.0 * ic_pruned / (ic_pruned + ic_not_pruned));
  }
}

void PKMeansThread::SortNeighbors(void) {
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    // Only do work that is mine
    if (c % num_threads_ != thread_index_) continue;
    vector< pair<float, unsigned int> > *v = &neighbor_vectors_[c];
    sort(v->begin(), v->end(), g_pfui_lower_compare);
  }
}

static void *thread_run_assign(void *v_t) {
  PKMeansThread *t = (PKMeansThread *)v_t;
  t->Assign();
  return NULL;
}

void PKMeansThread::RunAssign(void) {
  pthread_create(&pthread_id_, NULL, thread_run_assign, this);
}

static void *thread_run_pivot(void *v_t) {
  PKMeansThread *t = (PKMeansThread *)v_t;
  t->ComputeCentroidPivotDistances();
  return NULL;
}

static void *thread_run_intra(void *v_t) {
  PKMeansThread *t = (PKMeansThread *)v_t;
  t->ComputeIntraCentroidDistances();
  return NULL;
}

void PKMeansThread::RunPivot(void) {
  pthread_create(&pthread_id_, NULL, thread_run_pivot, this);
}

void PKMeansThread::RunIntra(void) {
  pthread_create(&pthread_id_, NULL, thread_run_intra, this);
}

static void *thread_run_sort(void *v_t) {
  PKMeansThread *t = (PKMeansThread *)v_t;
  t->SortNeighbors();
  return NULL;
}

void PKMeansThread::RunSort(void) {
  pthread_create(&pthread_id_, NULL, thread_run_sort, this);
}

void PKMeansThread::Join(void) {
  pthread_join(pthread_id_, NULL); 
}

// Use the KMeans++ method of seeding
void PKMeans::SeedPlusPlus(void) {
  bool *used = new bool[num_objects_];
  for (unsigned int o = 0; o < num_objects_; ++o) used[o] = false;
  // For the first centroid, choose one of the input objects at random
  unsigned int o = RandBetween(0, num_objects_ - 1);
  used[o] = true;
  for (unsigned int f = 0; f < dim_; ++f) {
    means_[0][f] = objects_[o][f];
  }
  double *sq_distance_to_nearest = new double[num_objects_];
  for (unsigned int o = 0; o < num_objects_; ++o) {
    if (used[o]) continue;
    double sq_dist = 0;
    float *obj = objects_[o];
    for (unsigned int d = 0; d < dim_; ++d) {
      double ov = obj[d];
      double cm = means_[0][d];
      float dim_delta = ov - cm;
      sq_dist += dim_delta * dim_delta;
    }
    sq_distance_to_nearest[o] = sq_dist;
  }
  for (unsigned int c = 1; c < num_clusters_; ++c) {
    if (c % 1000 == 0) {
      fprintf(stderr, "SeedPlusPlus: c %u/%u\n", c, num_clusters_);
    }
    double sum_min_sq_dist = 0;
    for (unsigned int o = 0; o < num_objects_; ++o) {
      if (used[o]) continue;
      sum_min_sq_dist += sq_distance_to_nearest[o];
    }
    double x = RandZeroToOne() * sum_min_sq_dist;
    double cum = 0;
    unsigned int o;
    for (o = 0; o < num_objects_; ++o) {
      if (used[o]) continue;
      cum += sq_distance_to_nearest[o];
      if (x <= cum) break;
    }
    used[o] = true;
    for (unsigned int f = 0; f < dim_; ++f) {
      means_[c][f] = objects_[o][f];
    }
    for (unsigned int o = 0; o < num_objects_; ++o) {
      if (used[o]) continue;
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
    }
  }
  delete [] sq_distance_to_nearest;
  delete [] used;
}

// Choose one item at random to serve as the seed of each cluster
void PKMeans::Seed1(void) {
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
void PKMeans::Seed2(void) {
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
void PKMeans::SingleObjectClusters(unsigned int num_clusters, unsigned int dim,
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

PKMeans::PKMeans(unsigned int num_clusters, unsigned int dim,
		 unsigned int num_objects, float **objects,
		 double neighbor_thresh, unsigned int num_pivots,
		 unsigned int num_threads) {
  nearest_centroids_ = NULL;
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
  num_pivots_ = num_pivots;
  if (num_pivots_ > num_clusters_) {
    fprintf(stderr, "Cannot have more pivots than clusters\n");
    exit(-1);
  }
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

  pivot_time_ = 0;
  intra_time_ = 0;
  assign_time_ = 0;

  // SeedPlusPlus() is pretty slow.  For now don't use when >= 10,000
  // clusters and more than 1m objects.  Could do 10k clusters and 3m objects
  // OK.
  if (num_clusters >= 10000 && num_objects > 1000000) {
    fprintf(stderr, "Calling Seed1\n");
    Seed1();
    fprintf(stderr, "Back from Seed1\n");
  } else {
    fprintf(stderr, "Calling SeedPlusPlus\n");
    SeedPlusPlus();
    fprintf(stderr, "Back from SeedPlusPlus\n");
  }

  // Use the first N clusters as the pivots.  I think this will be suitably
  // random.
  pivot_means_ = new float *[num_pivots_];
  for (unsigned int p = 0; p < num_pivots_; ++p) {
    pivot_means_[p] = new float[dim_];
    for (unsigned int d = 0; d < dim_; ++d) {
      pivot_means_[p][d] = means_[p][d];
    }
  }

  // This is a hack.  Nearest() ignores clusters with zero-size.  But for the
  // initial assignment, we don't want this.  Cluster sizes will get set
  // properly in Update().
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    cluster_sizes_[c] = 1;
  }

  centroid_pivot_distances_ = new float[num_clusters_ * num_pivots_];
  best_pivots_ = new unsigned int[num_clusters_];

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
#endif
    neighbor_vectors_ = new vector< pair<float, unsigned int> >[num_clusters_];
  } else {
    neighbor_vectors_ = NULL;
  }

  num_threads_ = num_threads;
  threads_ = new PKMeansThread *[num_threads_];
  for (unsigned int t = 0; t < num_threads_; ++t) {
    threads_[t] = new PKMeansThread(num_objects_, num_clusters_, objects_,
				    dim_, neighbor_thresh_, cluster_sizes_,
				    means_, num_pivots_, pivot_means_,
				    assignments_, nearest_centroids_,
				    centroid_pivot_distances_, best_pivots_, 
				    neighbor_vectors_, 0, t, num_threads_);
  }

  g_it = 0;
  // Normally we call this at the end of each iteration.  Call it once now
  // before the first iteration to speed up the first call to Assign().
  if (neighbor_thresh_ > 0) {
    fprintf(stderr, "Calling initial ComputeCentroidPivotDistances()\n");
    ComputeCentroidPivotDistances();
    fprintf(stderr, "Calling initial ComputeIntraCentroidDistances()\n");
    ComputeIntraCentroidDistances();
    fprintf(stderr,
	    "Back from initial call to ComputeIntraCentroidDistances()\n");
  }
}

// Assume caller owns objects
PKMeans::~PKMeans(void) {
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
  delete [] centroid_pivot_distances_;
  delete [] best_pivots_;
  delete [] neighbor_vectors_;
  delete [] cluster_sizes_;
  for (unsigned int p = 0; p < num_pivots_; ++p) {
    delete [] pivot_means_[p];
  }
  delete [] pivot_means_;
  for (unsigned int c = 0; c < num_clusters_; ++c) {
    delete [] means_[c];
  }
  delete [] means_;
  delete [] assignments_;
}

void PKMeans::ComputeCentroidPivotDistances(void) {
  time_t start_t = time(NULL);

  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->RunPivot();
  }
  // Execute thread 0 in main execution thread
  threads_[0]->ComputeCentroidPivotDistances();
  for (unsigned int i = 1; i < num_threads_; ++i) {
    threads_[i]->Join();
  }

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  pivot_time_ += diff_sec;
  fprintf(stderr, "Cum pivot time: %f\n", pivot_time_);
}

// Assumes cluster means are up-to-date
void PKMeans::ComputeIntraCentroidDistances(void) {
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
    sum_lens += neighbor_vectors_[c].size();
  }
  fprintf(stderr, "Avg neighbor vector length: %.1f\n",
	  sum_lens / (double)num_clusters_);

  time_t end_t = time(NULL);
  double diff_sec = difftime(end_t, start_t);
  intra_time_ += diff_sec;
  fprintf(stderr, "Cum intra time: %f\n", intra_time_);
}

unsigned int PKMeans::Assign(double *avg_dist) {
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

void PKMeans::Update(void) {
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

  // Using the first N clusters as the pivots.  I think this is suitably
  // random.  Update the pivot means here.  I suppose we could also leave
  // them unchanged.
  for (unsigned int p = 0; p < num_pivots_; ++p) {
    for (unsigned int d = 0; d < dim_; ++d) {
      pivot_means_[p][d] = means_[p][d];
    }
  }
}

void PKMeans::EliminateEmpty(void) {
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

void PKMeans::Cluster(unsigned int num_its) {
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
      ComputeCentroidPivotDistances();
      ComputeIntraCentroidDistances();
    }

    ++it;
  }
  EliminateEmpty();
}
