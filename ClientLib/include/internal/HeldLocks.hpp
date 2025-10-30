#pragma once

#include <set>
#include <common/BlobLocation.hpp>

namespace blobs::internal {



/** An internal class (not part of the client lib interface), which is used to manage currently held and sticky locks
 */
struct HeldLocks {
  std::set<BlobLocation> read, write;
};




}