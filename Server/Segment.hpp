#pragma once

#include "Cluster.hpp"

namespace blobs {
namespace server {

class Segment {
public:
  Segment(segment_id id);

  const segment_id id;
private:
  cluster_id lastClusterId;
  std::unordered_map<cluster_id, std::unique_ptr<Cluster>> clusters;
};

}}

