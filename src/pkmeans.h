#ifndef _PKMEANS_H_
#define _PKMEANS_H_

#include <vector>

using namespace std;

class PKMeansThread;

class PKMeans {
public:
  PKMeans(unsigned int num_clusters, unsigned int dim, unsigned int num_objects,
	  float **objects, double neighbor_thresh, unsigned int num_pivots,
	  unsigned int num_threads);
  ~PKMeans(void);
  void Cluster(unsigned int num_its);
  unsigned int Assignment(unsigned int o) const {return assignments_[o];}
  unsigned int NumClusters(void) const {return num_clusters_;}
  unsigned int ClusterSize(unsigned int c) const {return cluster_sizes_[c];}
  void SingleObjectClusters(unsigned int num_clusters, unsigned int dim,
			    unsigned int num_objects, float **objects);

  static const unsigned int kMaxNeighbors = 10000;


 protected:
  void ComputeCentroidPivotDistances(void);
  void ComputeIntraCentroidDistances(void);
  unsigned int Nearest(unsigned int o, float *obj) const;
  unsigned int Assign(double *avg_dist);
  void Update(void);
  void EliminateEmpty(void);
  void SeedPlusPlus(void);
  void Seed1();
  void Seed2();

  unsigned int num_objects_;
  unsigned int num_clusters_;
  float **objects_;
  unsigned int dim_;
  double neighbor_thresh_;
  unsigned int *cluster_sizes_;
  float **means_;
  unsigned int num_pivots_;
  float **pivot_means_;
  unsigned int *best_pivots_;
  unsigned int *assignments_;
  float *centroid_pivot_distances_;
  vector< pair<float, unsigned int> > *neighbor_vectors_;
  unsigned char **nearest_centroids_;
  bool use_shortcut_;
  double pivot_time_;
  double intra_time_;
  double assign_time_;
  unsigned int num_threads_;
  PKMeansThread **threads_;
};

#endif
