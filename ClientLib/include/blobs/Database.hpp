#pragma once

#include "Config.hpp"
#include "Session.hpp"
#include "Range.hpp"

#include <string>
#include <vector>
#include <memory>
#include <map>

namespace blobs {
struct BlobLocation;
class Transaction;

namespace network::message { 
  struct BlobsReadResponse;
}

namespace internal {
  struct HeldLocks;
}


class Database {
public:
  /** Opens a new database in the global session via the given connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be encoded in ASCII, the database name supports UTF-8 encoded file paths.
   */
  static Database* Open(std::string_view connectionString) {
    return Open(Session::GetGlobalSession(), connectionString.data(), connectionString.size());
  }

  /** Opens a new database in the specified session via the given connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be encoded in ASCII, the database name supports UTF-8 encoded file paths.
   */
  static Database* Open(const Session::Handle& session, std::string_view connectionString) {
    return Open(session, connectionString.data(), connectionString.size());
  }

  /** Opens a new database in the global session via the given unicode encoded connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be made up of only ASCII characters while the database name supports any unicode character.
   */
  static Database* Open(std::wstring_view u16ConnectionString) {
    return Open(Session::GetGlobalSession(), u16ConnectionString.data(), u16ConnectionString.size());
  }

  /** Opens a new database in the specified session via the given unicode encoded connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be made up of only ASCII characters while the database name supports any unicode character.
   */
  static Database* Open(const Session::Handle& session, std::wstring_view u16ConnectionString) {
    return Open(session, u16ConnectionString.data(), u16ConnectionString.size());
  }

  /** Opens a new database in the global session at the specified hostName with the given database name and optional port to use.
   * 
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-8 encoded)
   * @param port the port on which to connect to the database server
   */
  static Database* Open(std::string_view hostName, std::string_view databaseName, int port = 8108) {
    return Open(Session::GetGlobalSession(), hostName.data(), hostName.size(), databaseName.data(), databaseName.size(), port);
  }

  /** Opens a new database in the sepcified session at the specified hostName with the given database name and optional port to use.
   *
   * @param session the session to open the database in
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-8 encoded)
   * @param port the port on which to connect to the database server
   */
  static Database* Open(const Session::Handle& session, std::string_view hostName, std::string_view databaseName, int port = 8108) {
    return Open(session, hostName.data(), hostName.size(), databaseName.data(), databaseName.size(), port);
  }

  /** Opens a new database in the global session at the specified hostName with the given database name and optional port to use.
   *
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-16 encoded)
   * @param port the port on which to connect to the database server
   */
  static Database* Open(std::string_view hostName, std::wstring_view u16DatabaseName, int port = 8108) {
    return Open(Session::GetGlobalSession(), hostName.data(), hostName.size(), u16DatabaseName.data(), u16DatabaseName.size(), port);
  }

  /** Opens a new database in the specified session at the specified hostName with the given database name and optional port to use.
   *
   * @param session the session to open the database in
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-16 encoded)
   * @param port the port on which to connect to the database server
   */
  static Database* Open(const Session::Handle& session, std::string_view hostName, std::wstring_view u16DatabaseName, int port = 8108) {
    return Open(session, hostName.data(), hostName.size(), u16DatabaseName.data(), u16DatabaseName.size(), port);
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

  /** Convenience method to store string typed content in a blob
   */
  template<typename CharT = char>
  void WriteString(segment_id segment, cluster_id cluster, blob_id blob, const std::basic_string<CharT>& string) {
    WriteBlob(segment, cluster, blob, string.data(), string.size() * sizeof(CharT));
  }

  /** Convenience overload to allow the compiler to determine the template type from the argument
   */
  template<typename CharT = char>
  void WriteString(segment_id segment, cluster_id cluster, blob_id blob, const CharT* string) {
    WriteString(segment, cluster, blob, std::basic_string_view<CharT>(string));
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


  /** Convenience method to create a blob from the given string content
   */
  template<typename CharT = char>
  blob_id CreateString(segment_id segment, cluster_id cluster, std::basic_string_view<CharT> string) {
    return CreateBlob(segment, cluster, string.data(), string.size() * sizeof(CharT));
  }

  /** Convenience overload to allow the compiler to determine the template type from the argument
   */
  template<typename CharT = char>
  blob_id CreateString(segment_id segment, cluster_id cluster, const CharT* string) {
    return CreateString(segment, cluster, std::basic_string_view<CharT>(string));
  }

  /** Creates a new blob in the specified cluster and writes the passed data into it and then returns the id of the newly created blob.
   *  Only the first call to CreateBlob() for a given cluster inside a transaction will require server communication. All further 
   *  calls will be processed on the client only.
   * 
   * @throws exception::BlobLimitReached if no more blobs can be created in the specified cluster
   */
  BLOBS_EXPORT blob_id CreateBlob(segment_id segment, cluster_id cluster, const void* blobData, size_t blobSize);

  /** Creates a new cluster in the specified segment. The cluster will be initialized with an empty blob 0, which can be written to afterwards.
   */
  BLOBS_EXPORT cluster_id CreateCluster(segment_id segment);

  /** Creates a new segment in this database. The segment will be initialized with an empty cluster 0 containing an empty blob 0, which can be written to afterwards.
   */
  BLOBS_EXPORT segment_id CreateSegment();

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

  /** Retrieves a range with all existing blob ids in the specified cluster.
   *  This operation will set locks to prevent other clients form creating/deleting blobs in the same cluster.
   * 
   * @param segment the segment to query the blob list for
   * @param cluster the cluster to query the blob list for
   * @param writeLock if true then the blob list locks will be set as write locks instead of read locks
   */
  BLOBS_EXPORT Range<blob_id> GetAllBlobs(segment_id segment, cluster_id cluster, bool writeLock = false);

  /** Retrieves a range with all existing cluster ids in the specified segment.
   *  This operation will set locks to prevent other clients from creating/deleting clusters in the same segment.
   *
   * @param segment the segment to query the cluster list for
   * @param writeLock if true then the cluster list locks will be set as write locks instead of read locks
   */
  BLOBS_EXPORT Range<cluster_id> GetAllClusters(segment_id segment, bool writeLock = false);

  /** Retrieves a range of all existing segment ids in the database.
   *  This operation will set locks to prevent other clients from creating/deleting segments.
   * 
   * @param writeLock if true the segment list locks will be set as write locks instead of read locks
   */
  BLOBS_EXPORT Range<segment_id> GetAllSegments(bool writeLock = false);

  /** Closes the connection to this database and deletes this object.
   * 
   * @throws blobs::Exception if a transaction is currently in progress
   */
  BLOBS_EXPORT void Close();

  /** Returns a copy of the session handle, which identifies the session this database belongs to.
   */
  BLOBS_EXPORT Session::Handle GetSession() const;


  /** This method is called upon transaction commit to update the cached version of the blob, which has just been written to with the
   *  data committed to the database. That way the client will already have an up to date version of the blob and the server doesn't need to 
   *  send any blob data in the next transaction unless another client changed the blob in between the next transaction.
   */
  void UpdateCacheForCommittedBlob(const BlobLocation& location, std::vector<uint8_t> data, commit_id commitId, uint64_t transactionId);

  /** Called by Transaction::Commit() to clear some blobs from the cache (blob/cluster/segment id lists) if they were modified by the client
   *  to re-request them from the server to prevent them being out of sync. This is only relevant if the id list has been read during the same
   *  or a previous transaction where the blobs/clusters/segments have been created/deleted.
   * 
   *  An alternative solution to this would be to modify the cached blob list to avoid re-requesting it from the server after the commit as the 
   *  client SHOULD know the correct state of that list and SHOULD be able to keep track of the changes and modify the cache accordingly.
   *  This would however complicate the merging process of the server's/cached id list and the transactions state (created/deleted blobs/clusters/segments).
   */
  void RemoveCachedBlob(const BlobLocation& location);
  
  /** Internal method called during transaction commit to transfer ownership of sticky locks from the transaction into the database to be able
   *  to reuse them in the next transaction.
   */
  void AssignStickyLocks(std::unique_ptr<internal::HeldLocks> stickyLocks);


  const std::string name;
  const connection_id connectionId;
  const database_id id;
private:
  /** This method performs the actual read operation of ReadBlob() without checking for restricted ids.
   *  Read a blob with internal caching, which means that the client will ask the server to not resend the blob
   *  if the blob didn't change since the last transaction and if we are in the same transaction then the blob is simply
   *  returned without asking the server.
   *  The returned memory is pointing to the client's blob cache. To reference it safely, it should be copied out of the cache as soon as the
   *  call returns and it should never be written to. Use ReadString()/ReadVector() instead for more convenient data access.
   *
   * @throws exception::BlobDeleted if the blob has already been deleted in this transaction
   */
  std::pair<const void*, blob_size> ReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock);

  /** This method performs the actual write operation of WriteBlob() without checking for restricted ids.
   *  This method starts a transaction if not already started, acquires a write lock for the specified location (if not already done) and
   *  stores the data to write into the transaction's commit cache.
   *
   * @throws exception::BlobDeleted if the blob has already been deleted in this transaction
   */
  void WriteBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize);


  /** Private overload exported by the DLL and used by the std::string_view overload.
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const char* connectionString, size_t connectionStringLen);

  /** Private overload exported by the DLL to support UTF-16 encoded connection strings
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const wchar_t* connectionString, size_t connectionStringLen);


  /** Private overload exported by the DLL and used by the std::string_view overload. 
   *  We don't export string_view through the interface to avoid the risk of ABI incompatibilities
   *  for classes like std::string and std::string_view
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const char* hostName, size_t hostNameLen, const char* databaseName, size_t databaseNameLen, int port = 8108);
  
  /** Another private overload but for UTF16 encoded database paths
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const char* hostName, size_t hostNameLen, const wchar_t* databaseName, size_t databaseNameLen, int port = 8108);

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


  /** Returns the currently active transaction (if active) otherwise attempts to start a new transaction and
   *  returns the new transaction object upon success. Throws an exception if starting the transaction fails.
   */
  Transaction& GetTransaction();

  /** This method will transfer the sticky locks held by this database (if any) into the new transaction passed in as argument
   *  and will update all blobs in the internal blob cache for which sticky locks are held to the new transaction's id to make sure
   *  the client will actually use the cache for blobs for which he hold sticky locks (as they couldn't have possibly changed since the last transaction).
   */
  void ApplyStickyLocksToTansaction(Transaction& transaction);


  Database(const Session::Handle& session, std::string name, database_id id, connection_id connectionId);
  Database(const Database&) = delete;
  ~Database();
  Database& operator=(const Database&) = delete;

  class BlobCache;
  std::unique_ptr<BlobCache> cache;
  std::unique_ptr<internal::HeldLocks> stickyLocks; // Locks held past the end of the last transaction
  Session::Handle session; // the session, in which this database was created
};



}