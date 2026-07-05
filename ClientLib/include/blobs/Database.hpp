#pragma once

#include "Config.hpp"
#include "Session.hpp"
#include "Range.hpp"

#include <string_view>
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

/** The following enum can be passed to most read operations to specify what kind of lock will be acquired.
 *  The default being Read for read operations.
 */
enum class Lock {
  /** Perform a dirty read with no lock acquired. These read operations will never block and they will always request the 
   *  current state of the blob from teh server. The current transaction's cache will not be considered for this read operation
   *  and the cache will not be updated from the server's response. This read operation is essentially performed inside its own 
   *  isolated transaction and the returned value may be inconsistent with the blob's state in the current transaction.
   *  Only use this mode if avoiding locks is important and consistency between different reads is less so.
   */
  None = 0,
  
  /** Acquire an (upgradable) read lock for the read operation.
   */
  Read = 1, 
  
  /** Acquire a write lock with the read operation.This may be helpful to avoid read-write deadlocks
   *  or as a tool for explicitly synchronizing clients by write locking a specific resource.
   */
  Write = 2,     
};




class Database {
public:
  /** This enumeration can be passed to Database::Open() to control how the database is opened
   */
  enum class OpenMode {
    /** Open database and create the database if it doesn't exist yet (default)
     */
    CreateIfNotExist,

    /** Try to open the database and throw an exception if the database does not exist yet
     */
    OpenFailIfNotExist,

    /** Create a new database. Throws an exception if the database already exists
     */
    CreateFailIfExist,

    /** Always creates a new database. An existing database is overwritten with an empty database (WARNING: potential data loss!).
     *  This operation will fail if this database is currently opened by any other client.
     */
    CreateAlways
  };


  /** Opens a new database in the global session via the given connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be encoded in ASCII, the database name supports UTF-8 encoded file paths.
   */
  static Database* Open(std::string_view connectionString, OpenMode openMode = OpenMode::CreateIfNotExist, bool mvcc = false) {
    return Open(Session::GetGlobalSession(), connectionString.data(), connectionString.size(), openMode, mvcc);
  }

  /** Opens a new database in the specified session via the given connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be encoded in ASCII, the database name supports UTF-8 encoded file paths.
   */
  static Database* Open(const Session::Handle& session, std::string_view connectionString, OpenMode openMode = OpenMode::CreateIfNotExist, bool mvcc = false) {
    return Open(session, connectionString.data(), connectionString.size(), openMode, mvcc);
  }

  /** Opens a new database in the global session via the given unicode encoded connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be made up of only ASCII characters while the database name supports any unicode character.
   */
  static Database* Open(std::wstring_view u16ConnectionString, OpenMode openMode = OpenMode::CreateIfNotExist, bool mvcc = false) {
    return Open(Session::GetGlobalSession(), u16ConnectionString.data(), u16ConnectionString.size(), openMode, mvcc);
  }

  /** Opens a new database in the specified session via the given unicode encoded connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be made up of only ASCII characters while the database name supports any unicode character.
   */
  static Database* Open(const Session::Handle& session, std::wstring_view u16ConnectionString, OpenMode openMode = OpenMode::CreateIfNotExist, bool mvcc = false) {
    return Open(session, u16ConnectionString.data(), u16ConnectionString.size(), openMode, mvcc);
  }

  /** Opens a new database in the global session at the specified hostName with the given database name and optional port to use.
   * 
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-8 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   * @param mvcc if true then the database is set into MVCC transaction mode from be creation. This is for most part equivalent to calling
   *             Database::SetMVCC(true), except for database opened inside a transaction. Here SetMVCC() would only apply to the next transaction, not the current one.
   */
  static Database* Open(std::string_view hostName, std::string_view databaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108, bool mvcc = false) {
    return Open(Session::GetGlobalSession(), hostName.data(), hostName.size(), databaseName.data(), databaseName.size(), openMode, port, mvcc);
  }

  /** Opens a new database in the sepcified session at the specified hostName with the given database name and optional port to use.
   *
   * @param session the session to open the database in
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-8 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   * @param mvcc if true then the database is set into MVCC transaction mode from be creation. This is for most part equivalent to calling
   *             Database::SetMVCC(true), except for database opened inside a transaction. Here SetMVCC() would only apply to the next transaction, not the current one.
   */
  static Database* Open(const Session::Handle& session, std::string_view hostName, std::string_view databaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108, bool mvcc = false) {
    return Open(session, hostName.data(), hostName.size(), databaseName.data(), databaseName.size(), openMode, port, mvcc);
  }

  /** Opens a new database in the global session at the specified hostName with the given database name and optional port to use.
   *
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-16 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   * @param mvcc if true then the database is set into MVCC transaction mode from be creation. This is for most part equivalent to calling
   *             Database::SetMVCC(true), except for database opened inside a transaction. Here SetMVCC() would only apply to the next transaction, not the current one.
   */
  static Database* Open(std::string_view hostName, std::wstring_view u16DatabaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108, bool mvcc = false) {
    return Open(Session::GetGlobalSession(), hostName.data(), hostName.size(), u16DatabaseName.data(), u16DatabaseName.size(), openMode, port, mvcc);
  }

  /** Opens a new database in the specified session at the specified hostName with the given database name and optional port to use.
   *
   * @param session the session to open the database in
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-16 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   * @param mvcc if true then the database is set into MVCC transaction mode from be creation. This is for most part equivalent to calling
   *             Database::SetMVCC(true), except for database opened inside a transaction. Here SetMVCC() would only apply to the next transaction, not the current one.
   */
  static Database* Open(const Session::Handle& session, std::string_view hostName, std::wstring_view u16DatabaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108, bool mvcc = false) {
    return Open(session, hostName.data(), hostName.size(), u16DatabaseName.data(), u16DatabaseName.size(), openMode, port, mvcc);
  }



  /** Opens a new database in the global session in MVCC mode via the given connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be encoded in ASCII, the database name supports UTF-8 encoded file paths.
   */
  static Database* OpenMVCC(std::string_view connectionString, OpenMode openMode = OpenMode::CreateIfNotExist) {
    return Open(Session::GetGlobalSession(), connectionString.data(), connectionString.size(), openMode, true);
  }

  /** Opens a new database in the specified session in MVCC mode via the given connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be encoded in ASCII, the database name supports UTF-8 encoded file paths.
   */
  static Database* OpenMVCC(const Session::Handle& session, std::string_view connectionString, OpenMode openMode = OpenMode::CreateIfNotExist) {
    return Open(session, connectionString.data(), connectionString.size(), openMode, true);
  }

  /** Opens a new database in the global session in MVCC mode via the given unicode encoded connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be made up of only ASCII characters while the database name supports any unicode character.
   */
  static Database* OpenMVCC(std::wstring_view u16ConnectionString, OpenMode openMode = OpenMode::CreateIfNotExist) {
    return Open(Session::GetGlobalSession(), u16ConnectionString.data(), u16ConnectionString.size(), openMode, true);
  }

  /** Opens a new database in the specified session in MVCC mode via the given unicode encoded connection string of the following form:
   *  <host>[:<port>]/<dbName>
   *  Host and port must be made up of only ASCII characters while the database name supports any unicode character.
   */
  static Database* OpenMVCC(const Session::Handle& session, std::wstring_view u16ConnectionString, OpenMode openMode = OpenMode::CreateIfNotExist) {
    return Open(session, u16ConnectionString.data(), u16ConnectionString.size(), openMode, true);
  }

  /** Opens a new database in the global session in MVCC mode at the specified hostName with the given database name and optional port to use.
   *
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-8 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   */
  static Database* OpenMVCC(std::string_view hostName, std::string_view databaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108) {
    return Open(Session::GetGlobalSession(), hostName.data(), hostName.size(), databaseName.data(), databaseName.size(), openMode, port, true);
  }

  /** Opens a new database in the sepcified session in MVCC mode at the specified hostName with the given database name and optional port to use.
   *
   * @param session the session to open the database in
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-8 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   */
  static Database* OpenMVCC(const Session::Handle& session, std::string_view hostName, std::string_view databaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108) {
    return Open(session, hostName.data(), hostName.size(), databaseName.data(), databaseName.size(), openMode, port, true);
  }

  /** Opens a new database in the global session in MVCC mode at the specified hostName with the given database name and optional port to use.
   *
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-16 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   */
  static Database* OpenMVCC(std::string_view hostName, std::wstring_view u16DatabaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108) {
    return Open(Session::GetGlobalSession(), hostName.data(), hostName.size(), u16DatabaseName.data(), u16DatabaseName.size(), openMode, port, true);
  }

  /** Opens a new database in the specified session in MVCC mode at the specified hostName with the given database name and optional port to use.
   *
   * @param session the session to open the database in
   * @param database hostname/ip address to connect to (ASCII encoded)
   * @param databaseName path/filename of the database to open (UTF-16 encoded)
   * @param openMode how to open/create the database
   * @param port the port on which to connect to the database server
   */
  static Database* OpenMVCC(const Session::Handle& session, std::string_view hostName, std::wstring_view u16DatabaseName, OpenMode openMode = OpenMode::CreateIfNotExist, int port = 8108) {
    return Open(session, hostName.data(), hostName.size(), u16DatabaseName.data(), u16DatabaseName.size(), openMode, port, true);
  }





  /** Controls whether the NEXT transaction started for this database will be started as MVCC (Multi Version Concurrency Control) transaction.
   *  An MVCC transaction uses a snapshot taken at transaction start (or shortly before) and sets no locks, but also cannot perform any writes.
   *  The MVCC setting is per database and another database can be simultaneously opene in regular transaction mode. 
   * 
   *  Calling this method has NO EFFECT on the transaction mode of the currently running transaction of this database.
   * 
   * @param enable true = use mvcc, false = use regular write transaction (default)
   * 
   * @return the previous value of this setting
   */
  BLOBS_EXPORT bool SetMVCC(bool enable);

  /** Call this method to check whether the database in the currently active transaction is an mvcc transaction or a regular update transaction.
   *  The return value has no meaning if the database has no active transaction.
   */
  BLOBS_EXPORT bool IsMVCC() const;

  /** Returns true if this database takes part in the currently running transaction. This may return false even if Transaction::IsRunning() returns true,
   *  when having databases on multiple database servers open because transactions are only started on a server when a database is touched there.
   *  For a single server all databases start their transaction at the moment when the first database is touched.
   */
  BLOBS_EXPORT bool HasTransaction() const;

  /** Configures whether this database should use sticky locks or not.
   * 
   *  While enabled the locks from a previous transaction (which have not been revoked by the server) 
   *  are implicitly reacquired on the next transaction (without any added server communication).
   *  Sticky locks are enabled by default and can be disabled in use cases where they cause added locking conflicts.
   * 
   *  This setting takes effect right at the start of the next transaction and determines whether locks from a previous transaction
   *  are kept or discarded. After disabling sticky locks, the next transaction will be started with no locks held. 
   *  This option is ignored during MVCC transactions as they never hold any locks.
   * 
   * @param use true = enable sticky locks, false = disable.
   *            The default value is controlled by Transaction::UseStickyLocks(), which defaults to true upon initialization
   * 
   * @return the previous setting
   */
  BLOBS_EXPORT bool UseStickyLocks(bool use);


  /** Sets the lock timeout to use for read/write lock requests for this database. If a lock request cannot be satisfied
   *  within the specified time, an exception::LockTimeout will be thrown by the read/write operation.
   *  The default lock timeout for newly opened databases is defined by Transaction::SetLockTimeout(), which defaults to -1 (infinite).
   *  Setting the lock timeout immediately affects the timeout of the next read/write operation.
   * 
   * @param lockTimeoutMs the lock timeout to set in milliseconds.
   *                      0  = immediate timeout (lock acquisition fails immediately if the client would have to wait)
   *                           Setting an immediate timeout will NOT trigger a deadlock even if attempting to acquire the lock would result in a deadlock situation.
   *                           To trigger the deadlock you can set a near immediate timeout of 1ms.
   *                      -1 = infinite timeout
   * 
   * @return the previous lock timeout value.
   */
  BLOBS_EXPORT int32_t SetLockTimeout(int32_t lockTimeoutMs);

  /** A convenience access method to blob data, which returns the blob as std::string (or std::wstring).
   *  The method is implemented in the header and not exported to ensure the client can use any STL implementation he sees fit.
   */
  template<typename CharT = char>
  std::basic_string<CharT> ReadString(segment_id segment, cluster_id cluster, blob_id blob, Lock lock = Lock::Read) {
    auto [data, size] = ReadBlob(segment, cluster, blob, lock);
    return std::basic_string<CharT>(static_cast<const CharT*>(data), size / sizeof(CharT));
  }

  /** Similar to ReadString() this method reads a blob as vector of T.
   */
  template<typename T = uint8_t>
  std::vector<T> ReadVector(segment_id segment, cluster_id cluster, blob_id blob, Lock lock = Lock::Read) {
    auto [data, size] = ReadBlob(segment, cluster, blob, lock);
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
  BLOBS_EXPORT std::pair<const void*, blob_size> ReadBlob(segment_id segment, cluster_id cluster, blob_id blob, Lock lock = Lock::Read);


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
  template<typename CharT = char, typename Traits = std::char_traits<CharT>>
  blob_id CreateString(segment_id segment, cluster_id cluster, std::basic_string_view<CharT, Traits> string) {
    return CreateBlob(segment, cluster, string.data(), string.size() * sizeof(CharT));
  }

  /** Convenience overload to pass std::string directly wihtout the need to manually convert it into std::string_view.
   *  The compiler cannot derive the template arguments for std::basic_string_view from std::basic_string's type
   */
  template<typename CharT = char, typename Traits = std::char_traits<CharT>, typename Alloc = std::allocator<CharT>>
  blob_id CreateString(segment_id segment, cluster_id cluster, const std::basic_string<CharT, Traits, Alloc>& string) {
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
   *  Creating a blob requires a write lock on the cluster's blob id list (see GetAllBlobs()) and its next free cluster id.
   *  Upon creation the client implicitly acquires a write lock on the created blob.
   * 
   * @throws exception::BlobLimitReached if no more blobs can be created in the specified cluster
   */
  BLOBS_EXPORT blob_id CreateBlob(segment_id segment, cluster_id cluster, const void* blobData, size_t blobSize);


  /** Convenience method to create a blob from the given string content at a specified location
   */
  template<typename CharT = char, typename Traits = std::char_traits<CharT>>
  void CreateStringAt(segment_id segment, cluster_id cluster, blob_id blob, std::basic_string_view<CharT, Traits> string) {
    CreateBlobAt(segment, cluster, blob, string.data(), string.size() * sizeof(CharT));
  }

  /** Convenience overload to pass std::string directly wihtout the need to manually convert it into std::string_view.
   *  The compiler cannot derive the template arguments for std::basic_string_view from std::basic_string's type
   */
  template<typename CharT = char, typename Traits = std::char_traits<CharT>, typename Alloc = std::allocator<CharT>>
  void CreateStringAt(segment_id segment, cluster_id cluster, blob_id blob, const std::basic_string<CharT, Traits, Alloc>& string) {
    CreateBlobAt(segment, cluster, blob, string.data(), string.size() * sizeof(CharT));
  }

  /** Convenience overload to allow the compiler to determine the template type from the argument
   */
  template<typename CharT = char>
  void CreateStringAt(segment_id segment, cluster_id cluster, blob_id blob, const CharT* string) {
    CreateStringAt(segment, cluster, blob, std::basic_string_view<CharT>(string));
  }


  /** Creates a new blob at the specified blob id and writes the passed data into it. This method may also be used to re-create previously deleted blobs.
   *  This will influence the cluster's next free blob id and affect the id returned by CreateBlob().
   *  When creating a blob a an id higher than the cluster's current next free blob id, the next free blob id is updated, otherwise it stays unchanged.
   * 
   *  Creating a blob requires a write lock on the cluster's blob id list (see GetAllBlobs()) and its next free cluster id.
   *  Upon creation the client implicitly acquires a write lock on the created blob.
   * 
   * @throws exception::BlobAlreadyExists when calling this on an already existing blob
   */
  BLOBS_EXPORT void CreateBlobAt(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize);


  /** Creates a new cluster in the specified segment. The cluster will be initialized with an empty blob 0, which can be written to afterwards.
   * 
   *  Creating a cluster requires write locks on the segment's next free cluster id and the segment's cluster id list.
   *  Upon creating the cluster the client implicitly acquires write locks for the created cluster's next free blob id and blob id list
   *  as well as for implicitly created blob 0.
   */
  BLOBS_EXPORT cluster_id CreateCluster(segment_id segment);

  /** Creates a new segment in this database. The segment will be initialized with an empty cluster 0 containing an empty blob 0, which can be written to afterwards.
   * 
   *  Creating a segment requires write locks on the database's next free segment id and the database's segment id list.
   *  Upon creating the segment, the client implicitly acquires write locks for the created segment's next free cluster id and cluster id list
   *  as well as for the implicitly created cluster 0 the next free blob id and blob id list
   *  as well as for the implicitly created blob 0.
   */
  BLOBS_EXPORT segment_id CreateSegment();

  /** This method deletes a blob from the database, which is not the same as overwriting it with an empty blob.
   *  After a blob has been deleted, it can never be read/written again. 
   *  Deleting a blob requires a write lock for that blob and the cluster's blob id list (see GetAllBlobs())
   */
  BLOBS_EXPORT void DeleteBlob(segment_id segment, cluster_id cluster, blob_id blob);

  /** Acquires the lock needed to delete the cluster and deletes it, which will delete all its blobs with it.
   * 
   *  Deleting a cluster requires a write lock for the segment's cluster id list (see GetAllClusters()) as well as every blob in that cluster.
   */
  BLOBS_EXPORT void DeleteCluster(segment_id segment, cluster_id cluster);

  /** Acquires the lock needed to delete the segment and deletes it, which will delete all its clusters and blobs with it.
   * 
   *  Deleting a segment requires a write lock on the database's segment id list (see GetAllSegments()) as well as every cluster in that segment
   */
  BLOBS_EXPORT void DeleteSegment(segment_id segment);

  /** Retrieves a range with all existing blob ids in the specified cluster.
   *  This operation will set locks to prevent other clients form creating/deleting blobs in the same cluster.
   * 
   * @param segment the segment to query the blob list for
   * @param cluster the cluster to query the blob list for
   * @param lock what kind of lock to set on the blob id list during the read operation
   */
  BLOBS_EXPORT Range<blob_id> GetAllBlobs(segment_id segment, cluster_id cluster, Lock lock = Lock::Read);

  /** Retrieves a range with all existing cluster ids in the specified segment.
   *  This operation will set locks to prevent other clients from creating/deleting clusters in the same segment.
   *
   * @param segment the segment to query the cluster list for
   * @param lock what kind of lock to set on the blob id list during the read operation
   */
  BLOBS_EXPORT Range<cluster_id> GetAllClusters(segment_id segment, Lock lock = Lock::Read);

  /** Retrieves a range of all existing segment ids in the database.
   *  This operation will set locks to prevent other clients from creating/deleting segments.
   * 
   * @param lock what kind of lock to set on the blob id list during the read operation
   */
  BLOBS_EXPORT Range<segment_id> GetAllSegments(Lock lock = Lock::Read);

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
  std::pair<const void*, blob_size> ReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, Lock lock = Lock::Read);


  /** This method is called by ReadBlobInternal for dirty reads. It is much more simplified as doesn't check the transaction state/cache
   *  and always returns the blob straight from the server. Since this method cannot return the memory from the blob cache this also 
   *  means that the response must be handled differently.
   */
  std::pair<const void*, blob_size> DirtyReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob);

  /** This method performs the actual write operation of WriteBlob() without checking for restricted ids.
   *  This method starts a transaction if not already started, acquires a write lock for the specified location (if not already done) and
   *  stores the data to write into the transaction's commit cache.
   *
   * @throws exception::BlobDeleted if the blob has already been deleted in this transaction
   */
  void WriteBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize);

  /** Private overload exported by the DLL and used by the std::string_view overload.
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const char* connectionString, size_t connectionStringLen, OpenMode openMode, bool mvcc);

  /** Private overload exported by the DLL to support UTF-16 encoded connection strings
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const wchar_t* connectionString, size_t connectionStringLen, OpenMode openMode, bool mvcc);


  /** Private overload exported by the DLL and used by the std::string_view overload. 
   *  We don't export string_view through the interface to avoid the risk of ABI incompatibilities
   *  for classes like std::string and std::string_view
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const char* hostName, size_t hostNameLen, const char* databaseName, size_t databaseNameLen, OpenMode openMode, int port, bool mvcc);
  
  /** Another private overload but for UTF16 encoded database paths
   */
  BLOBS_EXPORT static Database* Open(const Session::Handle& session, const char* hostName, size_t hostNameLen, const wchar_t* databaseName, size_t databaseNameLen, OpenMode openMode, int port, bool mvcc);

  /** Sets the active mvcc mode to what was configured for this database (used when starting a new transaction) 
   *  and returns the mvcc mode for this database.
   */
  bool FixMVCC();

  /** Just creates the new blob without writing data into. After creation the client will be considered holding a write lock on that blob.
   *  This method can be called multiple times in a single transaction and only the first call (for this cluster) will actually require communication
   *  with the database server to facilitate efficient creation of multiple blobs in a single transaction.
   */
  blob_id CreateBlobInternal(segment_id segment, cluster_id cluster);

  /** Creates a new blob without writing data into it at the specified location.
   */
  void CreateBlobInternalAt(segment_id segment, cluster_id cluster, blob_id blob);


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


  Database(const Session::Handle& session, std::string name, database_id id, connection_id connectionId, bool mvcc);
  Database(const Database&) = delete;
  ~Database();
  Database& operator=(const Database&) = delete;

  class BlobCache;
  std::unique_ptr<BlobCache> cache;
  std::unique_ptr<internal::HeldLocks> stickyLocks; // Locks held past the end of the last transaction
  Session::Handle session; // the session, in which this database was created
  struct MVCC {
    bool active;  // During a transaction this value will be true if the tranaction for this database has been opened in MVCC mode (see IsMVCC)
    bool setting; // Controls whether the next transaction started for this database will be in MVCC mode (see SetMVCC)
  } mvcc;
  bool useStickyLocks; // controls whether this database automatically reacquires locks from the previous transaction

  /** The lock timeout in milliseconds for all read / write operations requiring a lock (0 = immediate timeout, -1 = infinite timeout[default])
   *  The default value is configured in Transaction::SetLockTimeout()
   */
  int32_t lockTimeoutMs; 
};



}