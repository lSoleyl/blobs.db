#pragma once

#include <common/BlobLocation.hpp>

namespace blobs::server {

/** For a lack of a better name this is simply called Deleted. 
 *  This structure keeps track of all deleted blobs,cluster,segments during transaction commit to 
 *  later release any held locks.
 */
struct Deleted {
  /** Returns true if the specified location is included in the list of deleted blobs, clusters, segments
   */
  bool IsDeleted(const BlobLocation& location) const;


  std::vector<BlobLocation> blobs;
  std::vector<std::pair<segment_id, cluster_id>> clusters;
  std::vector<segment_id> segments;
};


}