#pragma once

#include "Config.hpp"

#include <string>
#include <vector>
#include <memory>

namespace blobs {
struct BlobLocation;
namespace network::message { 
  struct BlobsReadResponse;
}


class Database {
public:
  /** Opens a new database via the given connection string of the following form:
   *  <host>[:<port>]/<dbName>
   */
  static Database* Open(std::string_view connectionString) {
    return Open(connectionString.data(), connectionString.size());
  }

  /** Opens a new database at the specified hostName with the given database name and optional port to use.
   */
  static Database* Open(std::string_view hostName, std::string_view databaseName, int port = 8108) {
    return Open(hostName.data(), hostName.size(), databaseName.data(), databaseName.size(), port);
  }


  /** A convenience access method to blob data, which returns the blob as std::string (or std::wstring).
   *  The method is implemented in the header and not exported to ensure the client can use any STL implementation he sees fit.
   */
  template<typename CharT = char>
  std::basic_string<CharT> ReadString(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock = false) {
    auto [data, size] = ReadBlob(segment, cluster, blob, writeLock);
    return std::basic_string<CharT>(static_cast<const CharT*>(data), size / sizeof(CharT));
  }

  /** Similar to ReadString() this method reads a blob as vector of T.
   */
  template<typename T = uint8_t>
  std::vector<T> ReadVector(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock = false) {
    auto [data, size] = ReadBlob(segment, cluster, blob, writeLock);
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
  BLOBS_EXPORT std::pair<const void*, blob_size> ReadBlob(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock = false);


  /** Convenience method to store string typed content in a blob
   */
  template<typename CharT = char>
  void WriteString(segment_id segment, cluster_id cluster, blob_id blob, std::basic_string_view<CharT> string) {
    WriteBlob(segment, cluster, blob, string.data(), string.size() * sizeof(CharT));
  }

  /** Convenience method to store a std::vector<T> in a blob
   */
  template<typename T>
  void WriteVector(segment_id segment, cluster_id cluster, blob_id blob, const std::vector<T>& data) {
    WriteBlob(segment, cluster, blob, data.data(), data.size() * sizeof(T));
  }

  /** This method starts a transaction if not already started, acquires a write lock for the specified location (if not already done) and
   *  stores the data to write into the transaction's commit cache.
   * 
   * @throws exception::BlobDeleted if the blob has already been deleted in this transaction
   */
  BLOBS_EXPORT void WriteBlob(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize);

  /** Creates a new blob in the specified cluster and writes the passed data into it and then returns the id of the newly created blob.
   *  Only the first call to CreateBlob() for a given cluster inside a transaction will require server communication. All further 
   *  calls will be processed on the client only.
   * 
   * @throws exception::BlobLimitReached if no more blobs can be created in the specified cluster
   */
  BLOBS_EXPORT blob_id CreateBlob(segment_id segment, cluster_id cluster, const void* blobData, size_t blobSize);

  /** This method deletes a blob from the database, which is not the same as overwriting it with an empty blob.
   *  After a blob has been deleted, it can never be read/written again. Deleting a blob requires a write lock for that blob.
   */
  BLOBS_EXPORT void DeleteBlob(segment_id segment, cluster_id cluster, blob_id blob);

  /** Acquires the lock needed to delete the cluster and deletes it, which will delete all its blobs with it.
   */
  BLOBS_EXPORT void DeleteCluster(segment_id segment, cluster_id cluster);

  /** Acquires the lock needed to delete the segment and deletes it, which will delete all its clusters and blobs with it.
   */
  BLOBS_EXPORT void DeleteSegment(segment_id segment);


  /** Closes the connection to this database and deletes this object.
   */
  BLOBS_EXPORT void Close();


  /** This method is called upon transaction commit to update the cached version of the blob, which has just been written to with the
   *  data committed to the database. That way the client will already have an up to date version of the blob and the server doesn't need to 
   *  send any blob data in the next transaction unless another client changed the blob in between the next transaction.
   */
  void UpdateCacheForCommittedBlob(const BlobLocation& location, std::vector<uint8_t> data, commit_id commitId, uint64_t transactionId);

  

  const std::string name;
  const connection_id connectionId;
  const database_id id;
private:
  /** Private overload exported by the DLL and used by the std::string_view overload.
   */
  BLOBS_EXPORT static Database* Open(const char* connectionString, size_t connectionStringLen);


  /** Private overload exported by the DLL and used by the std::string_view overload. 
   *  We don't export string_view through the interface to avoid the risk of ABI incompatibilities
   *  for classes like std::string and std::string_view
   */
  BLOBS_EXPORT static Database* Open(const char* hostName, size_t hostNameLen, const char* databaseName, size_t databaseNameLen, int port = 8108);

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

  class BlobCache;
  std::unique_ptr<BlobCache> cache;
};



}