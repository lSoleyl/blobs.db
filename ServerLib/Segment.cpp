#include "pch.hpp"
#include <server/Segment.hpp>
#include <server/MemoryBlockDelta.hpp>
#include <server/FileBackend.hpp>


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


Cluster* Segment::GetLoadedCluster(cluster_id clusterId, const FileBackend& file) {
  if (auto cluster = GetCluster(clusterId)) {
    // Load the cluster from file if it isn't loaded yet
    TODO("Once we use async IO to load stuff, we must handle LOADED and LOADING separately");
    if (cluster->status != Status::LOADED) {
      cluster->LoadFrom(file);
      assert(cluster->status == Status::LOADED);
    }

    return cluster;
  }

  return nullptr;
}


void Segment::LoadAllBlobs(const FileBackend& file) {
  assert(status == Status::LOADED); // the segment itself should already be loaded
  
  for (auto& [clusterId, cluster] : *this) {
    TODO("Once we use async IO to load stuff, we must handle LOADED and LOADING separately");
    if (cluster->status != Status::LOADED) {
      cluster->LoadFrom(file);
      assert(cluster->status == Status::LOADED);
    }

    // Now ensure all blobs of that cluster are loaded
    cluster->LoadAllBlobs(file);
  }
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


Blob* Segment::GetLoadedBlob(cluster_id cluster, blob_id blob, const FileBackend& file) {
  if (cluster == constants::NextFreeClusterId && blob == constants::NextFreeBlobId) {
    // Special blob holding the next free cluster id for cluster creation
    return &nextFreeClusterIdBlob;
  }

  if (auto clusterObj = GetLoadedCluster(cluster, file)) {
    // Use GetLoadedBlob to also handle the nextFreeBlobId and load the blob from file if necessary
    return clusterObj->GetLoadedBlob(blob, file);
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
    auto endId = startId;

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



void Segment::LoadFrom(const FileBackend& file) {
  assert(status != Status::LOADED); // We currently don't use the LOADING status, this will only be used once we migrate to async IO loading
  assert(file); // cannot be called on an in memory database (or one that failed to open)

  auto fileSegment = file.LoadMemoryBlock<file::Segment>(fileLocation);
  if (!fileSegment) {
    TODO("Better error handling?");
    throw std::exception("Failed to load segment!");
  }

  commitId = fileSegment->commitId;
  SetNextFreeClusterId(fileSegment->nextFreeClusterId);

  for (auto it = fileSegment->begin(), end = fileSegment->end(fileLocation.size); it != end; ++it) {
    auto& range = *it;
    auto clusterId = range.startId;
    for (auto& clusterReference : range) {
      DelayLoadCluster(clusterId, clusterReference);
      ++clusterId;
    }
  }

  status = Status::LOADED;
}


Segment::iterator Segment::begin() {
  assert(status == Status::LOADED);
  return clusters.begin();
}

Segment::iterator Segment::end() {
  assert(status == Status::LOADED);
  return clusters.end();
}



void Segment::DelayLoadCluster(cluster_id cluster, const file::BlockReference& fileLocation) {
  auto& clusterPtr = clusters[cluster];

  // This operation is not allowed for already existing/loaded clusters
  assert(!clusterPtr);

  clusterPtr = std::make_shared<Cluster>(cluster, std::numeric_limits<commit_id>::max() /* the commit id is stored inside the cluster's memory block, so we cannot know it yet */);
  clusterPtr->status = MemoryBlock::Status::NOT_LOADED;
  clusterPtr->fileLocation = fileLocation;
}




}}