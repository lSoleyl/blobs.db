#include "pch.hpp"
#include <internal/DatabasesState.hpp>

namespace blobs::internal {


DatabasesState::DatabasesState() {}

DatabasesState::~DatabasesState() {
  // No database should be still opened
  assert(openedDatabases.empty());
}

}