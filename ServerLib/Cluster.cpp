#include "pch.hpp"
#include "include/server/Cluster.hpp"
#include "include/server/MemoryBlockDelta.hpp"
#include "include/server/FileBackend.hpp"

namespace blobs {
namespace server {

Cluster::Cluster(cluster_id id, commit_id commitId) : id(id), commitId(commitId), nextFreeBlobId(0), nextFreeBlobIdBlob(constants::NextFreeBlobId, commitId) {
  nextFreeBlobIdBlob.SetIdContent(nextFreeBlobId);
}

Cluster::Cluster(const Cluster& other, commit_id commitId) : id(other.id), commitId(commitId), blobs(other.blobs),
                                                             nextFreeBlobId(other.nextFreeBlobId), nextFreeBlobIdBlob(constants::NextFreeBlobId, commitId) {
  nextFreeBlobIdBlob.SetIdContent(nextFreeBlobId);
}

Blob* Cluster::GetBlob(blob_id blob) {
  if (blob == constants::NextFreeBlobId) {
    // special blob holding the next free blob id
    TODO("Ensure the blob is always up to date with the actual `nextFreeBlobId`");
    return &nextFreeBlobIdBlob;
  }


  auto pos = blobs.find(blob);
  return (pos != blobs.end()) ? pos->second.get() : nullptr;
}

Blob* Cluster::UpdateBlob(blob_id blob, MemoryBlockDelta* delta) {
  auto& blobPtr = blobs[blob];
  if (!blobPtr || blobPtr->commitId != commitId) {
    if (delta && blobPtr) {
      // Mark the blob from the previous commit as released (will be replaced by a new one)
      delta->Released(blobPtr.get());
    }

    // Blob not yet created OR not yet modified in this transaction -> create an empty one
    blobPtr = std::make_shared<Blob>(blob, commitId);
    if (delta) {
      delta->Allocated(blobPtr.get());
    }
  }

  return blobPtr.get();
}

void Cluster::DeleteBlob(blob_id blob, MemoryBlockDelta* delta) {
  auto pos = blobs.find(blob);
  if (pos != blobs.end()) {
    if (delta) {
      delta->Released(pos->second.get());
    }
    blobs.erase(pos);
  }
}


void Cluster::ReleaseAllBlobs(MemoryBlockDelta* delta) {
  for (auto& [blobId, blob] : blobs) {
    delta->Released(blob.get());
  }
}


blob_id Cluster::GetNextFreeBlobId() const {
  return nextFreeBlobId;
}

void Cluster::SetNextFreeBlobId(blob_id nextFreeId) {
  nextFreeBlobId = nextFreeId;
  nextFreeBlobIdBlob.SetIdContent(nextFreeBlobId);
}


uint64_t Cluster::CalculateRequiredSize() const {
  uint64_t size = sizeof(file::Cluster);

  // To calculate the required file size, we need to determine the number of contiguous blob ids
  // This calculation is the reason why we opted for the sorted flat map instead of any other map as 
  // the sorted flat map makes this calculation easiest while still providing a good lookup performance.
  int contiguousBlocks = 0;
  std::optional<blob_id> lastBlobId;
  for (auto& [blobId, blob] : blobs) {
    if (!lastBlobId || blobId != *lastBlobId + 1) {
      // first block, or any following block
      ++contiguousBlocks;
    }
    lastBlobId = blobId;
  }

  // We have to allocate one BlobRange for each continguous range of block ids
  size += sizeof(file::Cluster::BlobRange) * contiguousBlocks;

  // And then allocate one block reference for each blob
  size += sizeof(file::BlockReference) * blobs.size();

  return size;
}


void Cluster::SerializeIntoBuffer(std::vector<char>& targetBuffer) const {
  // Assume the memory block has been correctly sized before calling this method
  assert(CalculateRequiredSize() == fileLocation.size);
  targetBuffer.resize(fileLocation.size);

  auto fileCluster = reinterpret_cast<file::Cluster*>(targetBuffer.data());
  fileCluster->commitId = commitId;
  fileCluster->nextFreeBlobId = nextFreeBlobId;
  fileCluster->id = id;


  auto rangePos = fileCluster->begin();
  
  auto blobsPos = blobs.begin();
  auto blobsEnd = blobs.end();
  while (blobsPos != blobsEnd) {
    // Find the next group of contiguous blobs
    auto startId = blobsPos->first;
    auto endId = rangePos->startId;

    auto writePos = rangePos->begin();
    *writePos++ = blobsPos->second->fileLocation; // store the block reference

    // Find the end of the contiguous blobs
    while (++blobsPos != blobsEnd && blobsPos->first == endId + 1) {
      ++endId;
      *writePos++ = blobsPos->second->fileLocation; // store the block reference
    }

    // Set the range of contiguous blobs and advance the ranges iterator
    rangePos->startId = startId;
    rangePos->endId = endId + 1;
    ++rangePos;
  }  
  
  // After writing the last blob range, we should end up at the end position
  assert(rangePos == fileCluster->end(fileLocation.size));
}



void Cluster::LoadFrom(const FileBackend& file) {
  assert(status != Status::LOADED); // We currently don't use the LOADING status, this will only be used once we migrate to async IO loading
  assert(file); // cannot be called on an in memory database (or one that failed to open)

  auto fileCluster = file.LoadMemoryBlock<file::Cluster>(fileLocation);
  if (!fileCluster) {
    TODO("Better error handling?");
    throw std::exception("Failed to load cluster!");
  }

  commitId = fileCluster->commitId;
  SetNextFreeBlobId(fileCluster->nextFreeBlobId);

  for (auto it = fileCluster->begin(), end = fileCluster->end(fileLocation.size); it != end; ++it) {
    auto& range = *it;
    auto blobId = range.startId;
    for (auto& blobReference : range) {
      DelayLoadBlob(blobId, blobReference);
      ++blobId;
    }
  }

  status = Status::LOADED;
}


void Cluster::DelayLoadBlob(blob_id blob, const file::BlockReference& fileLocation) {
  auto& blobPtr = blobs[blob];

  // This operation is not allowed for already existing/loaded clusters
  assert(!blobPtr);

  blobPtr = std::make_shared<Blob>(blob, std::numeric_limits<commit_id>::max() /* the commit id is stored inside the blob's memory block, so we cannot know it yet */);
  blobPtr->status = MemoryBlock::Status::NOT_LOADED;
  blobPtr->fileLocation = fileLocation;
}

}}