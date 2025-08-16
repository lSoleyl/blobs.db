#include "pch.hpp"
#include "include/server/Segment.hpp"


namespace blobs {
namespace server {

Segment::Segment(segment_id id, commit_id commitId) : id(id), commitId(commitId), nextFreeClusterId(0), nextFreeClusterIdBlob(constants::NextFreeBlobId, commitId) {
  nextFreeClusterIdBlob.SetIdContent(nextFreeClusterId);
}


Segment::Segment(const Segment& other, commit_id commitId) : id(other.id), commitId(commitId), clusters(other.clusters),
                                                             nextFreeClusterId(other.nextFreeClusterId), nextFreeClusterIdBlob(constants::NextFreeBlobId, commitId) {
  nextFreeClusterIdBlob.SetIdContent(nextFreeClusterId);
}


Cluster* Segment::GetCluster(cluster_id cluster) {
  // We cannot handle `NextFreeClusterId` here, because we have to return a whole cluster here.
  auto pos = clusters.find(cluster);
  return (pos != clusters.end()) ? pos->second.get() : nullptr;
}


Cluster* Segment::UpdateCluster(cluster_id cluster) {
  auto& clusterPtr = clusters[cluster];
  if (!clusterPtr) {
    // Cluster doesn't exist yet -> create it
    clusterPtr = std::make_shared<Cluster>(cluster, commitId);
  } else if (clusterPtr->commitId != commitId) {
    // Cluster not yet copied in this transaction -> do it now
    clusterPtr = std::make_shared<Cluster>(*clusterPtr, commitId);
  }

  return clusterPtr.get();
}

void Segment::DeleteCluster(cluster_id cluster) {
  clusters.erase(cluster);
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

cluster_id Segment::GetNextFreeClusterId() const {
  return nextFreeClusterId;
}

void Segment::SetNextFreeClusterId(cluster_id nextFreeId) {
  nextFreeClusterId = nextFreeId;
  nextFreeClusterIdBlob.SetIdContent(nextFreeClusterId);
}


uint64_t Segment::CalculateRequiredSize() const {
  uint64_t size = sizeof(file::Segment);

  // To calculate the required file size, we need to determine the number of contiguous cluster ids
  // This calculation is the reason why we opted for the sorted flat map instead of any other map as 
  // the sorted flat map makes this calculation easiest while still providing a good lookup performance.
  int contiguousBlocks = 0;
  std::optional<cluster_id> lastClusterId;
  for (auto& [clusterId, cluster] : clusters) {
    if (!lastClusterId || clusterId != *lastClusterId + 1) {
      // first block, or any following block
      ++contiguousBlocks;
    }
    lastClusterId = clusterId;
  }

  // We have to allocate one ClusterRange for each continguous range of cluster ids
  size += sizeof(file::Segment::ClusterRange) * contiguousBlocks;

  // And then allocate one block reference for each cluster
  size += sizeof(file::BlockReference) * clusters.size();

  return size;

  TODO("When writing the segment into file, assert that we don't write past the calculated required size");
}


}}