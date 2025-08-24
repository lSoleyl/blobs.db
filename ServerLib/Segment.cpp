#include "pch.hpp"
#include "include/server/Segment.hpp"
#include "include/server/MemoryBlockDelta.hpp"


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


Cluster* Segment::UpdateCluster(cluster_id cluster, MemoryBlockDelta* delta) {
  TODO("What if the segment isn't loaded yet? Can this happen?");

  auto& clusterPtr = clusters[cluster];
  if (!clusterPtr) {
    // Cluster doesn't exist yet -> create it
    clusterPtr = std::make_shared<Cluster>(cluster, commitId);
    // And implicitly create the empty blob 0
    clusterPtr->UpdateBlob(0, delta);
    clusterPtr->SetNextFreeBlobId(1);

    if (delta) {
      delta->Allocated(clusterPtr.get());
    }
  } else if (clusterPtr->commitId != commitId) {
    // Cluster not yet copied in this transaction -> do it now
    if (delta) {
      delta->Released(clusterPtr.get());
    }
    clusterPtr = std::make_shared<Cluster>(*clusterPtr, commitId);
    if (delta) {
      delta->Allocated(clusterPtr.get());
    }
  }

  return clusterPtr.get();
}

void Segment::DeleteCluster(cluster_id cluster, MemoryBlockDelta* delta) {
  auto pos = clusters.find(cluster);
  if (pos != clusters.end()) {
    if (delta) {
      delta->Released(pos->second.get());
      // Important: Explicitly mark all blobs of the cluster as released to not leak their memory
      pos->second->ReleaseAllBlobs(delta);
    }
    clusters.erase(pos);
  }
}

void Segment::ReleaseAllClusters(MemoryBlockDelta* delta) {
  for (auto& [clusterId, cluster] : clusters) {
    delta->Released(cluster.get());
    cluster->ReleaseAllBlobs(delta);
  }
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
}


void Segment::SerializeIntoBuffer(std::vector<char>& targetBuffer) const {
  // Assume the memory block has been correctly sized before calling this method
  assert(CalculateRequiredSize() == fileLocation.size);
  targetBuffer.resize(fileLocation.size);

  auto fileSegment = reinterpret_cast<file::Segment*>(targetBuffer.data());
  fileSegment->commitId = commitId;
  fileSegment->nextFreeClusterId = nextFreeClusterId;
  fileSegment->id = id;


  auto rangePos = fileSegment->begin();

  auto clustersPos = clusters.begin();
  auto clustersEnd = clusters.end();
  while (clustersPos != clustersEnd) {
    // Find the next group of contiguous clusters
    auto startId = clustersPos->first;
    auto endId = rangePos->startId;

    auto writePos = rangePos->begin();
    *writePos++ = clustersPos->second->fileLocation; // store the block reference

    // Find the end of the contiguous clusters
    while (++clustersPos != clustersEnd && clustersPos->first == endId + 1) {
      ++endId;
      *writePos++ = clustersPos->second->fileLocation; // store the block reference
    }

    // Set the range of contiguous clusters and advance the ranges iterator
    rangePos->startId = startId;
    rangePos->endId = endId + 1;
    ++rangePos;
  }

  // After writing the last cluster range, we should end up at the end position
  assert(rangePos == fileSegment->end(fileLocation.size));
}


}}