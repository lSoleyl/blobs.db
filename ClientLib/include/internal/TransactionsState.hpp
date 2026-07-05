#pragma once

#include <blobs/Transaction.hpp>

#include <map>

namespace blobs::internal {

/** The session local transaction state as returned by Session::Transactions().
 *  I would prefer to define this as Transaction::SessionState, but if I define this as nested class, then I cannot forward
 *  declare this type anymore and we will have a cyclic dependency as Transaction needs Session::Handle().
 */
class TransactionsState {
public:
  TransactionsState();
  ~TransactionsState();

  /** Returns true if at least one transaction is active in this session
   */
  bool IsRunning() const;

  std::map<connection_id, Transaction> active; // the currently active transactions (up to one per server connection)
  uint64_t nextId; // The next used transaction id in this session

  /** The transaction priority of this client (ses Transaction::SetPriority())
   */
  transaction_priority priority;

  /** The default lock timeout in milliseconds for the session. All newly opened databases will use this lock timeout.
   *  Default value is -1 = infinite timeout
   */
  int32_t lockTimeoutMs;

  /** The default setting of using sticky locks for newly opened databases. The actual setting is stored in the Database instance itself
   *  and can be overwritten any time (Database::UseStickyLocks()).
   * 
   *  A database with enabled sticky locks will attempt to keep all locks from the previous transaction when the next transaction starts
   *  except for the ones the server revoked between the two transactions.
   * 
   *  Default is true
   */
  bool useStickyLocks;

  /** The default setting of using MVCC tranaction mode for newly opened databases. The acutal MVCC mode is stored in the Database instance itself 
   *  and can be overwritten any time (Database::SetMVCC()).
   * 
   *  Default is false
   */
  bool useMVCC;

private:
  // Type is neither movable, nor copyable
  TransactionsState(const TransactionsState&) = delete;
  TransactionsState(TransactionsState&&) = delete;
  TransactionsState& operator=(const TransactionsState&) = delete;
  TransactionsState& operator=(TransactionsState&&) = delete;
};


}