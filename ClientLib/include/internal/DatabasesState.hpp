#pragma once

#include "..\blobs\Config.hpp"

#include <map>
#include <vector>

namespace blobs {
  class Database;
}


namespace blobs::internal {

class DatabasesState {
public:
  DatabasesState();
  ~DatabasesState();

  std::map<database_id, Database*> openedDatabases; // Used by Database class to keep track of all opened databases in the current session

  /** Used by Database::DirtyReadBlobInternal() to store the blob's content.
   *  We cannot store it in the blob cache because dirty reads do not update the blob cache.
   *  We also cannot store it in a static variable as this would prevent multiple threads from performing dirty reads simultaneously 
   *  in separate sessions unless I add additional synchronization. Storing it here will also neatly cleanup this memory when the session is deleted.
   */
  std::vector<uint8_t> dirtyReadBuffer;
private:
  // Type is neither movable, nor copyable
  DatabasesState(const DatabasesState&) = delete;
  DatabasesState(DatabasesState&&) = delete;
  DatabasesState& operator=(const DatabasesState&) = delete;
  DatabasesState& operator=(DatabasesState&&) = delete;
};


}
