#ifndef _UNIVARIATE_KMEANS_H_
#define _UNIVARIATE_KMEANS_H_

class UnivariateKMeans {
public:
  UnivariateKMeans(unsigned int num_objects, int *objects);
  ~UnivariateKMeans(void);
  void Cluster(unsigned int num_clusters, unsigned int num_its);
  unsigned int Assignment(unsigned int o) const {return assignments_[o];}
  double Mean(unsigned int c) const {return means_[c];}
  int WorstDist(void) const {return worst_dist_;}
  unsigned int NumClusters(void) const {return num_clusters_;}
private:
  unsigned int Assign(void);
  void Update(void);
  void EliminateEmpty(void);
  unsigned int BinarySearch(double r, unsigned int begin, unsigned int end,
			    long long int *cum_distance_to_nearest, bool *used);
  void SeedPlusPlus(void);
  
  unsigned int num_objects_;
  unsigned int num_clusters_;
  int *objects_;
  unsigned int *assignments_;
  double *means_;
  unsigned int *cluster_sizes_;
  int worst_dist_;
};

#endif
