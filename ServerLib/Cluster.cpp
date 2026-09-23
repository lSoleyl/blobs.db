#include "pch.hpp"
#include <server/Cluster.hpp>
#include <server/MemoryBlockDelta.hpp>
#include <server/FileBackend.hpp>
#include <server/LRUCache.hpp>

namespace blobs {
namespace server {

Cluster::Cluster(cluster_id id, commit_id commitId) : id(id), commitId(commitId), nextFreeBlobId(0), nextFreeBlobIdBlob(constants::NextFreeBlobId, commitId) {
  nextFreeBlobIdBlob.SetIdContent(nextFreeBlobId);
}

Cluster::Cluster(const Cluster& other, commit_id commitId) : id(other.id), commitId(commitId), blobs(other.blobs),
                                                             nextFreeBlobId(other.nextFreeBlobId), nextFreeBlobIdBlob(constants::NextFreeBlobId, commitId) {
  assert(other.status == Status::LOADED); // Otherwise the blobs copy will be incomplete
  nextFreeBlobIdBlob.SetIdContent(nextFreeBlobId);
}


Cluster::~Cluster() {
  OnDestroy();
}


Blob* Cluster::GetBlob(blob_id blob) {
  assert(status == Status::LOADED);

  auto pos = blobs.find(blob);
  return (pos != blobs.end()) ? pos->second.get() : nullptr;
}

Blob* Cluster::GetLoadedBlob(blob_id blob, const FileBackend& file) {
  assert(status == Status::LOADED);

  if (blob == constants::NextFreeBlobId) {
    // special blob holding the next free blob id
    TODO("Ensure the blob is always up to date with the actual `nextFreeBlobId`");
    return &nextFreeBlobIdBlob;
  }


  auto pos = blobs.find(blob);
  auto blobObj = (pos != blobs.end()) ? pos->second.get() : nullptr;
  if (blobObj) {
    // Load the blob from file if it isn't loaded yet
    TODO("Once we use async IO to load stuff, we must handle LOADED and LOADING separately");
    if (blobObj->status != Status::LOADED) {
      blobObj->LoadFrom(file);
      assert(blobObj->status == Status::LOADED);
    }
    LRUCache::Touch(*blobObj);
  }

  return blobObj;
}

void Cluster::LoadAllBlobs(const FileBackend& file) {
  assert(status == Status::LOADED);

  for (auto& [blobId, blob] : *this) {
    // Load the blob from file if it isn't loaded yet
    TODO("Once we use async IO to load stuff, we must handle LOADED and LOADING separately");
    if (blob->status != Status::LOADED) {
      blob->LoadFrom(file);
      assert(blob->status == Status::LOADED);
    }
  }

  // At the moment this is being called only when the cluster or its segment is deleted during transaction commit and
  // the mvcc snapshot still references it. No need to touch the cluster here, because
  // it will be removed from the LRU cache anyway upon commit.
}


Blob* Cluster::UpdateBlob(blob_id blob, MemoryBlockDelta* delta) {
  assert(status == Status::LOADED);

  // Update memory usage in case we change the size of the blobs map or allocate a new blob
  LRUCache::UpdateMemoryOnExit<Cluster> updateMemory(*this);

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

  // We only call UpdateBlob() on a Cluster copied through UpdateCluster() during transaction commit, 
  // which is thus not a part of the LRU cache, so no need to touch it here.
  return blobPtr.get();
}

void Cluster::DeleteBlob(blob_id blob, MemoryBlockDelta* delta) {
  assert(status == Status::LOADED);

  auto pos = blobs.find(blob);
  if (pos != blobs.end()) {
    // Update memory if we actually delete a blob
    LRUCache::UpdateMemoryOnExit<Cluster> updateMemory(*this);

    if (delta) {
      delta->Released(pos->second.get());
    }
    blobs.erase(pos);
  }

  // We only call DeleteBlob() on a newly copied Cluster during transaction commit, 
  // which is thus not a part of the LRU cache, so no need to touch it here.
}


void Cluster::ReleaseAllBlobs(MemoryBlockDelta* delta) {
  assert(status == Status::LOADED); // Otherwise we will not be able to release the memory allocated in the database file for all blobs

  for (auto& [blobId, blob] : blobs) {
    delta->Released(blob.get());
  }
}


blob_id Cluster::GetNextFreeBlobId() const {
  assert(status == Status::LOADED);

  return nextFreeBlobId;
}

void Cluster::SetNextFreeBlobId(blob_id nextFreeId) {
  assert(status == Status::LOADED);

  nextFreeBlobId = nextFreeId;
  nextFreeBlobIdBlob.SetIdContent(nextFreeBlobId);
}


uint64_t Cluster::CalculateRequiredSize() const {
  assert(status == Status::LOADED);

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
    auto endId = startId;

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

  status = Status::LOADED; // set status up here to not trigger the assertion in SetNextFreeBlobId
  commitId = fileCluster->commitId;
  SetNextFreeBlobId(fileCluster->nextFreeBlobId);

  // Enter the cluster with the current size of 0 into the LRU cache
  LRUCache::Enter(*this);

  // Then when exiting LoadFrom() we will update total memory and evictable memory accordingly
  LRUCache::UpdateMemoryOnExit<Cluster> updateMemory(*this);

  for (auto it = fileCluster->begin(), end = fileCluster->end(fileLocation.size); it != end; ++it) {
    auto& range = *it;
    auto blobId = range.startId;
    for (auto& blobReference : range) {
      DelayLoadBlob(blobId, blobReference);
      ++blobId;
    }
  }
}

void Cluster::AllocatedInFile() {
  LRUCache::Enter(*this);
}

size_t Cluster::EvictSize() const {
  // The total evitctable memory consists of the allocated blob map entries and the actually contained Blob objects.
  // We don't consider the std::shared_ptr control block here.
  return blobs.capacity() * sizeof(decltype(blobs)::entry) + blobs.size() * sizeof(Blob);
}

void Cluster::Evict() {
  // Move an empty map into our map to assure that the memory allocated allocated by std::vector is actually deallocated
  blobs = decltype(blobs){};
  status = Status::NOT_LOADED;
}


Cluster::iterator Cluster::begin() {
  assert(status == Status::LOADED);
  return blobs.begin();
}

Cluster::iterator Cluster::end() {
  assert(status == Status::LOADED);
  return blobs.end();
}


void Cluster::DelayLoadBlob(blob_id blob, const file::BlockReference& fileLocation) {
  auto& blobPtr = blobs[blob];

  // This operation is not allowed for already existing/loaded clusters
  assert(!blobPtr);

  blobPtr = std::make_shared<Blob>(blob, std::numeric_limits<commit_id>::max() /* the commit id is stored inside the blob's memory block, so we cannot know it yet */);
  blobPtr->status = Status::NOT_LOADED;
  blobPtr->fileLocation = fileLocation;
}

}}