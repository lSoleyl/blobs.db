#pragma once

#include "Config.hpp"

#include <string>

namespace blobs {

class Database {
public:
  /** Opens a new database via the given connection string.
   */
  BLOBS_EXPORT static Database* Open(const char* connectionString);

  //TODO: ReadBlob()
  //TODO: what should be the return value of ReadBlob()? to be cross dll compatible?
  //TODO: WriteBlob()

  /** Closes the connection to this database and deletes this object.
   */
  BLOBS_EXPORT void Close();

private:
  Database(std::string name, uint8_t id, size_t connectionId);
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  std::string name;
  size_t connectionId;
  uint8_t id;
};



}