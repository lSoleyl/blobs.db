#include "pch.hpp"
#include "Segment.hpp"


namespace blobs {
namespace server {

Segment::Segment(uint32_t id) : id(id), lastClusterId(0) {
  clusters.emplace(0, std::make_unique<Cluster>(0));
}

Cluster* Segment::GetCluster(cluster_id cluster) {
  TODO("Handle `NextFreeClusterId` similar to Cluster");
  auto pos = clusters.find(cluster);
  return (pos != clusters.end()) ? pos->second.get() : nullptr;
}


}}