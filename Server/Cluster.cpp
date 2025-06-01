#include "pch.hpp"
#include "Cluster.hpp"

namespace blobs {
namespace server {

Cluster::Cluster(cluster_id id, commit_id commitId) : id(id), commitId(commitId), nextFreeBlobId(1), nextFreeBlobIdBlob(constants::NextFreeBlobId, commitId) {
  TODO("Later load the blobs from the database file");
  blobs.emplace(0, std::make_shared<Blob>(0));
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


}}