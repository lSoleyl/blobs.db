#include "pch.hpp"
#include "Cluster.hpp"

namespace blobs {
namespace server {

Cluster::Cluster(cluster_id id) : id(id), nextFreeBlobId(1), nextFreeBlobIdBlob(constants::NextFreeBlobId) {
  blobs.emplace(0, std::make_unique<Blob>(0));
  nextFreeBlobIdBlob.setContent(nextFreeBlobId);
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


blob_id Cluster::GetNextFreeBlobId() const {
  return nextFreeBlobId;
}

}}