#pragma once

#include "..\MessagePointer.hpp"

namespace blobs {
namespace network {
namespace message {


/** Send by the client to abort any currently running transaction and release all locks held by this client on the server
 */
struct TransactionAbort : public Message {
public:

  /** Constructs a new TransactionAbort message
   */
  static MessagePointer Create();

  static constexpr Type type = Type::TransactionAbort;
private:
  TransactionAbort();
};

}}}
