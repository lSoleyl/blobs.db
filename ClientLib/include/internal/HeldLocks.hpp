#pragma once

#include <set>
#include <common/BlobLocation.hpp>

namespace blobs::internal {



/** An internal class (not part of the client lib interface), which is used to manage currently held and sticky locks
 */
struct HeldLocks {
  std::set<BlobLocation> read, write;

  /** Updates the list of held locks by either keeping only the locks specified in the range (if keep = true)
   *  Or by removing all locks specified in the range (if keep = false);
   */
  void UpdateLocks(bool keep, const BlobLocation* begin, const BlobLocation* end);
};




}