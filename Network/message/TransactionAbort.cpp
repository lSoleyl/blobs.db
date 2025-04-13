#include <network/message/TransactionAbort.hpp>

namespace blobs {
namespace network {
namespace message {

TransactionAbort::TransactionAbort() : Message(sizeof(TransactionAbort), TransactionAbort::type) {}

MessagePointer TransactionAbort::Create() {
  return MessagePointer(new TransactionAbort);
}




}}}
