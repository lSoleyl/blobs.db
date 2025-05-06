#pragma once
#include "Config.hpp"
#include <memory>


namespace blobs {
struct BlobLocation;

/** Instances of this class represent a single global transaction.
 */
class Transaction {
  Transaction();
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


  /** This function is called if a transaction is aborted by the server because of a deadlock.
   *  The client will obviously not inform the server about that
   */
  static void AbortDeadlock();

  /** Returns the currently running transaction or nullptr if no transaction is running.
   *  This is only used from within the ClientLib itself
   *
   * @param startIfNotActive if true implicitly starts a new transaction if now transaction is already running
   */
  static Transaction* Get(bool startIfNotActive = false);


  enum class LockMode { None, Read, Write };

  /** Returns the type of lock, which the client holds for the specified database location in the current transaction
   */
  LockMode GetLockType(database_id dbId, const BlobLocation& location) const;

  /** Each transaction has an id, which is counted up. This is not the same as the commit id of the server, which is stored in the blobs.
   *  This id is only used to determine, whether a cached blob has been read in the current transaction or a previous one.
   */
  const uint64_t id;
private:

  static std::unique_ptr<Transaction> current; // the currently active transaction
  static uint64_t nextId;

  // I am not very fond of this additional indirection, but it is the best way to keep the whole implementation details from the client
  // and also to avoid moving too many headers into the ClientLib itself
  struct State;
  std::unique_ptr<State> state; // The transaction state (held locks, outstanding writes)

  TODO("Add the blobs, which have been written/deleted and should be written back upon a commit")
};


}
