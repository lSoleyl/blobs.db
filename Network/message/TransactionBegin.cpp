#include <network/message/TransactionBegin.hpp>

#include <cassert>

namespace blobs::network::message {

TransactionBegin::TransactionBegin(message_size messageSize, transaction_priority transactionPriority) : Message(messageSize, TransactionBegin::type), transactionPriority(transactionPriority) {}

MessagePointer_T<TransactionBegin> TransactionBegin::Create(transaction_priority transactionPriority, int nDatabases) {
  assert(nDatabases < std::numeric_limits<database_id>::max()); // Something wrong with the client code

  auto messageSize = sizeof(TransactionBegin) + nDatabases * sizeof(DatabaseTxnMode);
  return MessagePointer_T<TransactionBegin>(new (new char[messageSize]) TransactionBegin(messageSize, transactionPriority));
}


auto TransactionBegin::begin() -> iterator {
  return reinterpret_cast<iterator>(this + 1);
}

auto TransactionBegin::begin() const -> const_iterator {
  return reinterpret_cast<const_iterator>(this + 1);
}

auto TransactionBegin::end() -> iterator {
  return reinterpret_cast<iterator>(reinterpret_cast<uint8_t*>(this) + size);
}

auto TransactionBegin::end() const -> const_iterator {
  return reinterpret_cast<const_iterator>(reinterpret_cast<const uint8_t*>(this) + size);
}

void TransactionBegin::DatabaseTxnMode::Set(database_id dbId, bool keepStickyLocks, bool mvcc) {
  this->dbId = dbId;
  this->txnMode = mvcc ? TxnMode::MVCC : keepStickyLocks ? TxnMode::UpdateKeepStickyLocks : TxnMode::UpdateDiscardStickyLocks;
}

std::ostream& operator<<(std::ostream& out, const TransactionBegin& message) {
  out << message.type << "(prio=" << message.transactionPriority << ", ";

  bool first = true;
  for (auto& entry : message) {
    if (!first) {
      out << ", ";
    }
    out << "db(" << static_cast<int>(entry.dbId) << ")=" << entry.txnMode;
  }

  return out << ')';
}

std::ostream& operator<<(std::ostream& out, TransactionBegin::TxnMode mode) {
  switch (mode) {
    case TransactionBegin::TxnMode::UpdateKeepStickyLocks: return out << "UpdateSticky";
    case TransactionBegin::TxnMode::UpdateDiscardStickyLocks: return out << "Update";
    case TransactionBegin::TxnMode::MVCC: return out << "MVCC";
  }

  assert(false); // unhandled type
  return out << "???";
}

}
