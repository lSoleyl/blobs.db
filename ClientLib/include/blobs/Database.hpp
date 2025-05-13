#pragma once

#include "Config.hpp"

#include <string>
#include <vector>

namespace blobs {
struct BlobLocation;
namespace network::message { 
  struct BlobsReadResponse;
}


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
   * 
   * @throws exception::BlobDeleted if the blob has already been deleted in this transaction
   */
  BLOBS_EXPORT std::pair<const void*, blob_size> ReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock = false);


  /** This method starts a transaction if not already started, acquires a write lock for the specified location (if not already done) and
   *  stores the data to write into the transaction's commit cache.
   * 
   * @throws exception::BlobDeleted if the blob has already been deleted in this transaction
   */
  BLOBS_EXPORT void WriteBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize);

  /** Creates a new blob in the specified cluster and writes the passed data into it and then returns the id of the newly created blob.
   *  Only the first call to CreateBlob() for a given cluster inside a transaction will require server communication. All further 
   *  calls will be processed on the client only.
   * 
   * @throws exception::BlobLimitReached if no more blobs can be created in the specified cluster
   */
  BLOBS_EXPORT blob_id CreateBlob(segment_id segment, cluster_id cluster, const void* blobData, size_t blobSize);

  /** This method deletes a blob from the database, which is not the same as overwriting it with an empty blob.
   *  After a blob has been deleted, it can never be read/written again.
   */
  BLOBS_EXPORT void DeleteBlob(segment_id segment, cluster_id cluster, blob_id blob);


  /** Closes the connection to this database and deletes this object.
   */
  BLOBS_EXPORT void Close();

  class BlobCache;

private:
  /** Just creates the new blob without writing data into. After creation the client will be considered holding a write lock on that blob.
   *  This method can be called multiple times in a single transaction and only the first call (for this cluster) will actually require communication
   *  with the database server to facilitate efficient creation of multiple blobs in a single transaction.
   */
  blob_id CreateBlobInternal(segment_id segment, cluster_id cluster);


  /** This internal helper method will acquire/upgrade to a write lock without requesting the blob's contents.
   */
  void WriteLockNoContent(const BlobLocation& location);

  /** Performs error handling when reading blobs, which just comes down to dispatching the correct exception and sometimes aborting a running transaction
   */
  void HandleReadBlobErrorResponse(const network::message::BlobsReadResponse& response);


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