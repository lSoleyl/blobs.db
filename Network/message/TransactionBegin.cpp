
#include <network/message/TransactionBegin.hpp>

namespace blobs::network::message {

TransactionBegin::TransactionBegin(bool keepStickyLocks) : Message(sizeof(TransactionBegin), TransactionBegin::type), keepStickyLocks(keepStickyLocks) {
  TODO("Add parameter for what kind of transaction to start (regular/mvcc)");
}

MessagePointer TransactionBegin::Create(bool keepStickyLocks) {
  return MessagePointer(new TransactionBegin(keepStickyLocks));
}




}
