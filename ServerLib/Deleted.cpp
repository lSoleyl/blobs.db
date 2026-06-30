#include "pch.hpp"
#include <server/Deleted.hpp>

namespace blobs::server {


bool Deleted::IsDeleted(const BlobLocation& location) const {
  if (std::find(segments.begin(), segments.end(), location.segment) != segments.end()) {
    return true;
  }

  if (std::find(clusters.begin(), clusters.end(), std::make_pair(location.segment, location.cluster)) != clusters.end()) {
    return true;
  }

  if (std::find(blobs.begin(), blobs.end(), location) != blobs.end()) {
    return true;
  }

  return false;
}


}