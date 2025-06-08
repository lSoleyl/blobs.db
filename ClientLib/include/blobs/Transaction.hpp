#pragma once
#include "Config.hpp"
#include <memory>
#include <optional>
#include <map>


namespace blobs {
struct BlobLocation;
class Database;

/** Instances of this class represent a single global transaction.
 */
class Transaction {
  Transaction(connection_id connectionId);
public:
  /** Returns true if a transaction is currently in progress
   */
  BLOBS_EXPORT static bool IsRunning();

  /** Commits the currently running transaction (if any). 
   * @return true if a transaction was running and was commited.
   */
  BLOBS_EXPORT static bool Commit();

  /** Aborts the currently running transaction (if any).
   * @return true if a transaction was running and was aborted.
   */
  BLOBS_EXPORT static bool Abort();


  /** This method is called if a transaction is aborted by the server because of a deadlock.
   *  The client will obviously not inform the server about that. But if the client has connections to 
   *  other servers, they will be informed about the transaction abort.
   *  After calling this method, the transaction object doesn't exist anymore.
   */
  void AbortDeadlock();

  /** Returns the currently running transaction or nullptr if no transaction is running.
   *  This is only used from within the ClientLib itself
   *
   * @param startIfNotActive if true implicitly starts a new transaction if now transaction is already running
   */
  static Transaction* Get(connection_id connectionId, bool startIfNotActive);


  enum class LockMode { None, Read, Write };

  /** Returns the type of lock, which the client holds for the specified database location in the current transaction
   */
  LockMode GetLockType(Database* database, const BlobLocation& location) const;

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


  /** Each transaction has an id, which is counted up. This is not the same as the commit id of the server, which is stored in the blobs.
   *  This id is only used to determine, whether a cached blob has been read in the current transaction or a previous one.
   */
  const uint64_t id;
private:

  static std::map<connection_id, Transaction> active; // the currently active transactions (up to one per server connection)
  static uint64_t nextId;

  // I am not very fond of this additional indirection, but it is the best way to keep the whole implementation details from the client
  // and also to avoid moving too many headers into the ClientLib itself
  struct State;
  std::unique_ptr<State> state; // The transaction state (held locks, outstanding writes)

  const connection_id connectionId;
};


}
