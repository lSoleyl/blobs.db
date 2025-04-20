#pragma once

#include "Cluster.hpp"

namespace blobs {
namespace server {

class Segment {
public:
  Segment(segment_id id);

  /** Returns the cluster with the specified id or nullptr if it doesn't exist
   */
  Cluster* GetCluster(cluster_id cluster);

  const segment_id id;
private:
  cluster_id lastClusterId;
  std::unordered_map<cluster_id, std::unique_ptr<Cluster>> clusters;
};

}}

