
#include <network/message/TransactionBegin.hpp>

namespace blobs::network::message {

TransactionBegin::TransactionBegin() : Message(sizeof(TransactionBegin), TransactionBegin::type) {
  TODO("Add parameter for what kind of transaction to start (regular/mvcc)");
  TODO("Add option for releasing locks later");
}

MessagePointer TransactionBegin::Create() {
  return MessagePointer(new TransactionBegin);
}




}
