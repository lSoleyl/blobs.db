#pragma once

#include "Config.hpp"

#include <string>
#include <vector>

namespace blobs {

class Database {
public:
  /** Opens a new database via the given connection string.
   */
  BLOBS_EXPORT static Database* Open(const char* connectionString);

  

  /** A convenience access method to blob data, which returns the blob as std::string.
   *  The method is implemented in the header and not exported to ensure the client can use any STL implementation he sees fit.
   */
  std::string ReadString(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock = false) {
    auto [data, size] = ReadBlobInternal(segment, cluster, blob, writeLock);
    return std::string(static_cast<const char*>(data), size);
  }

  /** Similar to ReadString() this method reads a blob as vector of T.
   */
  template<typename T = uint8_t>
  std::vector<T> ReadVector(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock = false) {
    auto [data, size] = ReadBlobInternal(segment, cluster, blob, writeLock);
    return std::vector<T>(static_cast<const T*>(data), static_cast<const T*>(data) + (size / sizeof(T)));
  }



  /** Read a blob with internal caching, which means that the client will ask the server to not resend the blob
   *  if the blob didn't change since the last transaction and if we are in the same transaction then the blob is simply
   *  returned without asking the server.
   *  The returned memory is pointing to the client's blob cache. To reference it safely, it should be copied out of the cache as soon as the
   *  call returns and it should never be written to. Use ReadString()/ReadVector() instead for more convenient data access.
   */
  BLOBS_EXPORT std::pair<const void*, blob_size> ReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock = false);

  TODO("Implement WriteBlob()")
  TODO("Implement DeleteBlob()")

  /** Closes the connection to this database and deletes this object.
   */
  BLOBS_EXPORT void Close();

  class BlobCache;

private:
  Database(std::string name, database_id id, connection_id connectionId);
  Database(const Database&) = delete;
  ~Database();
  Database& operator=(const Database&) = delete;

  std::string name;
  BlobCache* cache;
  connection_id connectionId;
  database_id id;
};



}