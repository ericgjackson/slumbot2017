#ifndef _KMEANS_H_
#define _KMEANS_H_

#include <vector>

using namespace std;

class KMeansThread;

class KMeans {
public:
  KMeans(unsigned int num_clusters, unsigned int dim, unsigned int num_objects,
	 float **objects, double neighbor_thresh, unsigned int num_threads);
  ~KMeans(void);
  void Cluster(unsigned int num_its);
  unsigned int Assignment(unsigned int o) const {return assignments_[o];}
  unsigned int NumClusters(void) const {return num_clusters_;}
  unsigned int ClusterSize(unsigned int c) const {return cluster_sizes_[c];}
  void SingleObjectClusters(unsigned int num_clusters, unsigned int dim,
			    unsigned int num_objects, float **objects);

  static const unsigned int kMaxNeighbors = 10000;


 protected:
  void ComputeIntraCentroidDistances(void);
  unsigned int Nearest(unsigned int o, float *obj) const;
  unsigned int Assign(double *avg_dist);
  void Update(void);
  void EliminateEmpty(void);
  unsigned int BinarySearch(double r, unsigned int begin, unsigned int end,
			    double *cum_sq_distance_to_nearest, bool *used);
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
  unsigned int *assignments_;
  vector< pair<float, unsigned int> > *neighbor_vectors_;
  unsigned char **nearest_centroids_;
  unsigned char **neighbor_ptrs_;
  bool use_shortcut_;
  double intra_time_;
  double assign_time_;
  unsigned int num_threads_;
  KMeansThread **threads_;
};

#endif
