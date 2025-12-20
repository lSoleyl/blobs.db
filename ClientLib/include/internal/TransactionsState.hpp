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

private:
  // Type is neither movable, nor copyable
  TransactionsState(const TransactionsState&) = delete;
  TransactionsState(TransactionsState&&) = delete;
  TransactionsState& operator=(const TransactionsState&) = delete;
  TransactionsState& operator=(TransactionsState&&) = delete;
};


}