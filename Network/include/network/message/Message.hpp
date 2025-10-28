#pragma once

#include <cstdint>
#include <string>
#include <iostream>

// This include path is horrible, but since the client lib headers are the ones being distributed and should be self contained,
// its headers must be the single source of truth to prevent copying.
#include "../../ClientLib/include/blobs/Config.hpp"

namespace blobs {
namespace network {
namespace message {

/** The type of network message
 */
enum class Type : uint8_t {
  DatabaseOpen,
  DatabaseOpenResponse,
  DatabaseClose,
  DatabaseCloseResponse,

  BlobsRead,
  BlobsReadResponse,

  TransactionBegin,         // Each transaction has to be started by the client
  TransactionBeginResponse, // The server notifies the client about successful start of transaction and which sticky locks to release/keep
  TransactionAbort,
  TransactionCommit, // Client transmits blobs in this message to the server (it has a continuation flag in case we need multiple messages)
  TransactionCommitResponse, // Response sent by the server after processing a commit to notify about whether the commit was accepted or not
  TransactionSetMode, // Sets the default transaction mode (for a specified database) (regular/MVCC) for all implicitly started transactions

  ConnectionOpened, // internally used message to notify the server about a new client connection
  ConnectionClosed, // internally used message to indicate that the network socket connection has been closed
  NetworkException, // internally used to throw an exception from the network thread in the main processing thread in AwaitMessage()
};

std::ostream& operator<<(std::ostream& out, Type messageType);

/** Format for a generic network message, which all messages have to follow
 */
struct Message {
  message_size size; // total length of the message in bytes. This includes this header and the content bytes
  Type type;     // kind/class of message

  /** This field identifies the client a message is from / for and is only used internally
   *  by the server and is ignored in client/server communication.
   */
  client_id clientId; 

  // Everything following is the payload

  /** Returns a string representation of the message for debugging purposes
   */
  std::string ToString() const;
protected:
  /** Provide a constructor to ensure the concrete message types initialize all Message fields
   */
  Message(message_size size, Type type);
};

std::ostream& operator<<(std::ostream& out, const Message& message);

}}}
