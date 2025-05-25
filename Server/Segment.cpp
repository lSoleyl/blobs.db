#include "pch.hpp"
#include "Segment.hpp"


namespace blobs {
namespace server {

Segment::Segment(segment_id id, commit_id commitId) : id(id), commitId(commitId), nextFreeClusterId(1), nextFreeClusterIdBlob(constants::NextFreeBlobId, commitId) {
  clusters.emplace(0, std::make_shared<Cluster>(0, commitId));
  nextFreeClusterIdBlob.setContent(nextFreeClusterId);
}


Segment::Segment(const Segment& other, commit_id commitId) : id(other.id), commitId(commitId), clusters(other.clusters),
                                                             nextFreeClusterId(other.nextFreeClusterId), nextFreeClusterIdBlob(constants::NextFreeBlobId, commitId) {
  nextFreeClusterIdBlob.setContent(nextFreeClusterId);
}


Cluster* Segment::GetCluster(cluster_id cluster) {
  // We cannot handle `NextFreeClusterId` here, because we have to return a whole cluster here.
  auto pos = clusters.find(cluster);
  return (pos != clusters.end()) ? pos->second.get() : nullptr;
}


Blob* Segment::GetBlob(cluster_id cluster, blob_id blob) {
  if (cluster == constants::NextFreeClusterId && blob == constants::NextFreeBlobId) {
    // Special blob holding the next free cluster id for cluster creation
    return &nextFreeClusterIdBlob;
  }

  if (auto clusterObj = GetCluster(cluster)) {
    return clusterObj->GetBlob(blob);
  }
  return nullptr;
}


}}