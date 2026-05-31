#pragma once
#include "Config.hpp"
#include "Session.hpp"

#include <memory>
#include <optional>


namespace blobs {
struct BlobLocation;
class Database;

namespace internal {
  struct HeldLocks;
}

/** Instances of this class represent a single global transaction.
 */
class Transaction {
  Transaction(const Session::Handle& session, connection_id connectionId);
public:
  ~Transaction();
  Transaction(Transaction&&) = default; // needed only for active.emplace(....)

  /** Returns true if a transaction is currently in progress in the specified session
   */
  BLOBS_EXPORT static bool IsRunning(const Session::Handle& session);

  /** Returns true if a transaction is currently running in the global session
   */
  static bool IsRunning() { return IsRunning(Session::GetGlobalSession()); }

  /** Commits the currently running transaction (if any in the specified session). 
   * @return true if a transaction was running and was commited.
   */
  BLOBS_EXPORT static bool Commit(const Session::Handle& session);


  /** Commits the currently running transaction (if any) in the global session.
   * @return true if a transaction was running and was commited.
   */
  static bool Commit() { return Commit(Session::GetGlobalSession()); }

  /** Aborts the currently running transaction (if any in the specified session).
   * @return true if a transaction was running and was aborted.
   */
  BLOBS_EXPORT static bool Abort(const Session::Handle& session);

  /** Aborts the currently running transaction (if any) in the global session.
   * @return true if a transaction was running and was aborted.
   */
  static bool Abort() { return Abort(Session::GetGlobalSession()); }


  /** Configures the default sticky lock mode for the given session.
   *  The default sticky lock mode is applied to all databases opened AFTER the setting has been changed.
   * 
   *  While enabled the locks from a previous transaction (which have not been revoked by the server) 
   *  are implicitly reacquired on the next transaction (without any added server communication).
   *  Sticky locks are enabled by default and can be disabled in use cases where they cause added locking conflicts.
   * 
   *  After disabling sticky locks, the next transaction will be started with no locks held.
   * 
   * @param session the session to configure the sticky lock default for
   * @param use if true the mechanism is enabled, false disabled (default is true)
   * 
   * @return the previous setting for sticky lock usage
   */
  BLOBS_EXPORT static bool UseStickyLocks(const Session::Handle& session, bool use);

  /** Enables/disables sticky lock usage for the global session
   */
  static bool UseStickyLocks(bool use) { return UseStickyLocks(Session::GetGlobalSession(), use); }


  /** Configures this client's transaction priority for all transactions started AFTER this call.
   *  In case of a deadlock the server will abort the transaction of the client with the smaller transaction priority.
   *  If both clients have the same transaction priority, the deadlock victim is chosen at random.
   *  The transaction priority has no meaning outside of deadlock resolution.
   *  The default transaction priority is 0.
   * 
   * @param session the session to configure the transaction priority for
   * @param transactionPriority the transaction priority to configure
   * 
   * @return the previously configured transaction priority
   */
  BLOBS_EXPORT static transaction_priority SetPriority(const Session::Handle& session, transaction_priority transactionPriority);

  /** Sets the tranaction priority for the global session
   */
  static transaction_priority SetPriority(transaction_priority transactionPriority) { return SetPriority(Session::GetGlobalSession(), transactionPriority); }


  /** This method is called if a transaction is aborted by the server because of a deadlock.
   *  The client will obviously not inform the server about that. But if the client has connections to 
   *  other servers, they will be informed about the transaction abort.
   *  After calling this method, the transaction object doesn't exist anymore.
   */
  void AbortDeadlock();

  /** Returns the currently running transaction or nullptr if no transaction is running.
   *  This is only used from within the ClientLib itself.
   * 
   * @param session the session to which the transaction belongs
   */
  static Transaction* Get(const Session::Handle& session, connection_id connectionId);


  /** Creates a new transaction for the given connection ID without checking for an already existing one. This 
   *  function is only used from within CLientLib itself.
   *  Database::GetTransaction() already handles the logic of starting a new transaction on demand the first time a database is 
   *  accessed outside of an transaction. This also implies that a server whose database is not being touched during a transaction won't
   *  be notified about the transaction. Notifying all connected servers immediately about the started transaction may result in a more consistent
   *  view of cross server databases (cannot be guaranteed due to timing issues), but would also mean that sticky locks would be actively held and
   *  could block other clients even though the database on the other server is not even being touched during the transaction.
   * 
   * @param session the session to which the created transaction will belong (the session should already be locked before calling this function)
   */
  static Transaction& Create(const Session::Handle& session, connection_id connectionId);


  enum class LockMode { None, Read, Write };

  /** Returns the type of lock, which the client holds for the specified database location in the current transaction
   */
  LockMode GetLockType(Database* database, const BlobLocation& location) const;


  /** This method should be called when a database attempts to perform the first data access inside a new transaction.
   *  It should pass its sticky locks to the transaction to carry them over from a previous one.
   *  The transaction will take ownership of the passed locks.
   */
  void UseStickyLocks(Database* database, std::unique_ptr<internal::HeldLocks> stickyLocks);

  /** Stores the acquired lock type for the specified blob in this transaction. This method must be internally called to
   *  correctly keep track of all currently held locks. This method ensures that locking invariants are followed. 
   *  Calling AcquiredLock() with a read lock after already calling it with a write lock will not downgrade the existing lock.
   */
  void AcquiredLock(Database* database, const BlobLocation& location, LockMode lock);


  /** Stores the specified blob data in the transaction's commit cache to transfer to the server upon transaction commit.
   *  The transaction copies the specified data into an internal buffer, so the blobData pointer doesn't have to stay valid 
   *  until the end of the transaction.
   * 
   * @throws exception::BlobDeleted when attempting to write blob data for a blob, which has already been deleted in this transaction
   */
  void WriteBlob(Database* database, const BlobLocation& location, const void* blobData, blob_size blobSize);


  /** Marks the blob as created during this transaction. This information is necessary to correctly keep track of the list of all
   *  blobs in a cluster during a transaction.
   */
  void CreateBlob(Database* database, const BlobLocation& location);

  /** Marks the cluster as created during this transaction. This information is necessary to correctly keep track of the list of all
   *  clusters in a segment during a transaction
   */
  void CreateCluster(Database* database, segment_id segment, cluster_id cluster);

  /** Marks the segment as created during this transaction. This information is necessary to correctly keep track of the list of all 
   *  segments in the database during a transaction
   */
  void CreateSegment(Database* database, segment_id segment);

  /** Returns true if the specified blob, its cluster or its segment has been just created in this transaction and not committed yet to the server.
   */
  bool IsCreatedBlob(Database* database, const BlobLocation& location) const;

  /** Returns true if the specified cluster or its segment has been just created in this transaction and not committed yet to the server.
   */
  bool IsCreatedCluster(Database* database, segment_id segment, cluster_id cluster) const;

  /** Returns true if the specified segment has been just created in this transaction and not committed yet to the server.
   */
  bool IsCreatedSegment(Database* database, segment_id segment) const;

  /** Marks a blob for deletion on transaction commit. After calling this method, the blob cannot be read or written anymore unless the
   *  transaction is aborted.
   */
  void DeleteBlob(Database* database, const BlobLocation& location);

  /** Marks a cluster for deletion on transaction commit. After calling this method no blob in that cluster can be read/written/created or deleted anymore
   *  unless the transaction is aborted.
   */
  void DeleteCluster(Database* database, segment_id segment, cluster_id cluster);


  /** Marks a segment for deletion on transaction commit. After calling this method no blob/cluster in that segment can be read/written/created or deleted anymore
   */
  void DeleteSegment(Database* database, segment_id segment);


  /** Reads the blob data from the transaction's write cache. If the blob has been written to in this transaction it will return 
   *  the blob's content. Returns an empty optional if the blob has not been written to in this transaction.
   *
   * @throws exception::BlobDeleted when attempting to read a blob, which has been deleted in this transaction
   */
  std::optional<std::pair<const void* /*data*/, blob_size>> ReadBlob(Database* database, const BlobLocation& location) const;

  /** This function is used to implement the merging of the server's blob id list with the local changes to it during this transaction.
   *  It will erase all blob ids of blobs deleted during this transaction and then insert ids for all blobs created during this transaction.
   */
  void MergeBlobIdList(Database* database, segment_id segment, cluster_id cluster, std::vector<blob_id>& blobs);

  /** This function is used to implement the merging of the server's cluster id list with the local changes to it during this transaction.
   *  It will erase all cluster ids of clusters deleted during this transaction and then insert ids for all clusters created during this transaction.
   */
  void MergeClusterIdList(Database* database, segment_id segment, std::vector<cluster_id>& clusters);

  /** This function is used to implement the merging of the server's segment id list with the local changes to it during this transaction.
   *  It will erase all segment ids of segments deleted during this transaction and then insert ids for all segments created during this transaction.
   */
  void MergeSegmentIdList(Database* database, std::vector<segment_id>& segments);


  /** Each transaction has an id, which is counted up. This is not the same as the commit id of the server, which is stored in the blobs.
   *  This id is only used to determine, whether a cached blob has been read in the current transaction or a previous one.
   */
  const uint64_t id;
private:

  /** Used as final step in a transaction commit/abort. This will transfer all held locks as sticky locks into the corresponding databases to be
   *  used for the next transaction and finally clean up all transaction state, which will implicitly destroy all transaction objects.
   */
  static void TransferAndClearState(const Session::Handle& session);

  /** This method is used as part of TransferAndClearState() to transfer out all sticky locks from one transaction into their corresponding databases
   */
  void TransferStickyLocks();

  // I am not very fond of this additional indirection, but it is the best way to keep the whole implementation details from the client
  // and also to avoid moving too many headers into the ClientLib itself
  struct State;
  std::unique_ptr<State> state; // The transaction state (held locks, outstanding writes)
  Session::Handle session; // This transaction's session

  const connection_id connectionId;

  // Not copyable
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
};

}
