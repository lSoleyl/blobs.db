#pragma once

#include "..\MessagePointer.hpp"

namespace blobs::network::message {


/** Send by the client to start a new transaction. 
 *  The client can specify to relase any still held sticky locks upon transaction begin.
 *  Independent of the client's intentions to release locks, the server will respond with a 
 *  TransactionBeginResult, which will specify, which locks the client may keep, or has to release.
 */
struct TransactionBegin : public Message {
  /** Constructs a new TransactionBegin message
   */
  static MessagePointer Create();

  static constexpr Type type = Type::TransactionBegin;
private:
  TransactionBegin();
};

}
