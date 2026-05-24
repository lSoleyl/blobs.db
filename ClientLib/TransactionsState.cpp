#include "pch.hpp"
#include <internal/TransactionsState.hpp>

namespace blobs::internal {


TransactionsState::TransactionsState() : nextId(0), useStickyLocks(true), useMVCC(false) {}

TransactionsState::~TransactionsState() {
  // This shouldn't be possible as each transaction holds a session handle to the session, so 
  // the transactions must be deleted before the session can be deleted
  assert(!IsRunning());
}


bool TransactionsState::IsRunning() const {
  return !active.empty();
}

}