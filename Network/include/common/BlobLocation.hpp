#pragma once

// This include path is horrible, but since the client lib headers are the ones being distributed and should be self contained,
// its headers must be the single source of truth to prevent copying.
#include "../../ClientLib/include/blobs/Config.hpp"
#include <xhash>
#include <iostream>

namespace blobs {

/** This structure defines the default way to refer to blobs in a database. As this is commonly used
 *  across multiple data structures across the project it has been moved into its own header
 */
struct BlobLocation {
  BlobLocation();
  BlobLocation(segment_id segment, cluster_id cluster, blob_id blob);

  segment_id segment;
  cluster_id cluster;
  blob_id blob;

  // Comparsion and ordering of blobs
  bool operator==(const BlobLocation& other) const;
  bool operator!=(const BlobLocation& other) const;
  bool operator<(const BlobLocation& other) const;
  bool operator<=(const BlobLocation& other) const;
  bool operator>(const BlobLocation& other) const;
  bool operator>=(const BlobLocation& other) const;
};

std::ostream& operator<<(std::ostream& out, const BlobLocation& location);

}


// Implementation of std::hash
template<>
struct std::hash<blobs::BlobLocation>
{
  size_t operator()(const blobs::BlobLocation& location) const noexcept;
};

