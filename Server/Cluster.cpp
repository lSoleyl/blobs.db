#include "pch.hpp"
#include "Cluster.hpp"

namespace blobs {
namespace server {

Cluster::Cluster(uint32_t id) : id(id), lastBlobId(0) {
  blobs.emplace(0, std::make_unique<Blob>(0));
}


}}