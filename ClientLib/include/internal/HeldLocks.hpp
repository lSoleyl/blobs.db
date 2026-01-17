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


  /** Releases all locks, which fall into the range [begin;end) where the range is defined as
   *  all blobs with begin <= location < end (i.e. begin and end are NOT iterators)
   * 
   * @param begin all locks for blobs at this location and after it are released
   * @param end all locks for blobs before this location are released
   */
  void ReleaseLockRange(const BlobLocation& begin, const BlobLocation& end);
};




}