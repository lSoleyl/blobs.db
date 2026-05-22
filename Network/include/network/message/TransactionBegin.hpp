#pragma once

#include "..\MessagePointer.hpp"

namespace blobs::network::message {


/** Send by the client to start a new transaction. 
 *  The client can specify to relase any still held sticky locks upon transaction begin.
 *  Independent of the client's intentions to release locks, the server will respond with a 
 *  TransactionBeginResult, which will specify, which locks the client may keep, or has to release.
 */
struct TransactionBegin : public Message {

  /** How to begin the transaction for the specified database
   */
  enum class TxnMode: uint8_t {
    UpdateKeepStickyLocks,
    UpdateDiscardStickyLocks,
    MVCC // implies no sticky locks
  };

  /** Structures of this type will be allocated past the end of this message to specify the transaction mode for each database
   */
  struct DatabaseTxnMode {
    database_id dbId;
    TxnMode txnMode;

    /** A simple setter performing the translation into the enum values
     */
    void Set(database_id dbId, bool keepStickyLocks, bool mvcc);
  };

  /** Constructs a new TransactionBegin message for the specified number of opened client databases
   */
  static MessagePointer_T<TransactionBegin> Create(int nDatabases);


  using iterator = DatabaseTxnMode*;
  using const_iterator = const DatabaseTxnMode*;

  /** First DatabaseTxnMode entry of this message
   */
  iterator begin();
  const_iterator begin() const;


  /** Past end DatabaseTxnMode entry of this message
   */
  iterator end();
  const_iterator end() const;

  static constexpr Type type = Type::TransactionBegin;
private:
  TransactionBegin(message_size mesageSize);
};

std::ostream& operator<<(std::ostream& out, const TransactionBegin& message);
std::ostream& operator<<(std::ostream& out, TransactionBegin::TxnMode mode);

}
