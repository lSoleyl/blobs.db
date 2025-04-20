#include "pch.hpp"
#include "Cluster.hpp"

namespace blobs {
namespace server {

Cluster::Cluster(cluster_id id) : id(id), lastBlobId(0) {
  blobs.emplace(0, std::make_unique<Blob>(0));
}

Blob* Cluster::GetBlob(blob_id blob) {
  auto pos = blobs.find(blob);
  return (pos != blobs.end()) ? pos->second.get() : nullptr;
}

}}