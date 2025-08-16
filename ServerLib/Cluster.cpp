#include "pch.hpp"
#include "include/server/Cluster.hpp"

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

Blob* Cluster::UpdateBlob(blob_id blob) {
  auto& blobPtr = blobs[blob];
  if (!blobPtr || blobPtr->commitId != commitId) {
    // Blob not yet created OR not yet modified in this transaction -> create an empty one
    blobPtr = std::make_shared<Blob>(blob, commitId);
  }

  return blobPtr.get();
}

void Cluster::DeleteBlob(blob_id blob) {
  blobs.erase(blob);
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

  TODO("When writing the cluster into file, assert that we don't write past the calculated required size");
}



}}