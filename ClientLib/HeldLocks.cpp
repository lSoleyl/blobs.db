#include "pch.hpp"
#include <internal/HeldLocks.hpp>


namespace blobs::internal {


void HeldLocks::UpdateLocks(bool keep, const BlobLocation* pos, const BlobLocation* end) {
  if (keep) {
    HeldLocks allLocks = std::move(*this);

    for (; pos != end; ++pos) {
      auto& lock = *pos;
      if (allLocks.read.count(lock)) {
        read.insert(lock);
      } else if (allLocks.write.count(lock)) {
        write.insert(lock);
      }
    }
  } else {
    // Erase all specified locks
    for (; pos != end; ++pos) {
      auto& lock = *pos;
      read.erase(lock);
      write.erase(lock);
    }
  }
}


}

