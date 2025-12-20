#pragma once

#include "..\blobs\Config.hpp"

#include <map>

namespace blobs {
  class Database;
}


namespace blobs::internal {

class DatabasesState {
public:
  DatabasesState();
  ~DatabasesState();

  std::map<database_id, Database*> openedDatabases; // Used by Database class to keep track of all opened databases in the current session

private:
  // Type is neither movable, nor copyable
  DatabasesState(const DatabasesState&) = delete;
  DatabasesState(DatabasesState&&) = delete;
  DatabasesState& operator=(const DatabasesState&) = delete;
  DatabasesState& operator=(DatabasesState&&) = delete;
};


}
