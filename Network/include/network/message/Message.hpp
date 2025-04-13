#pragma once

#include <cstdint>

namespace blobs {
namespace network {
namespace message {

/** The type of network message
 */
enum class Type : uint8_t {
  DatabaseOpen,
  DatabaseOpenResponse,
  DatabaseClose, // No response, because the server will simply confirm by replying with the same DatabaseClose message
  
  BlobsRead,

  TransactionAbort,
  TransactionCommit, //TODO: what if we want to commit more than 2GB of data? How to split such a transaction commit into multiple parts?

  ConnectionOpened, // internally used message to notify the server about a new client connection
  ConnectionClosed, // internally used message to indicate that the network socket connection has been closed
  NetworkException, // internally used to throw an exception from the network thread in the main processing thread in AwaitMessage()
};


/** Format for a generic network message, which all messages have to follow
 */
struct Message {
  uint32_t size; // total length of the message in bytes. This includes this header and the content bytes
  Type type;     // kind/class of message

  /** This field identifies the client a message is from / for and is only used internally
   *  by the server and is ignored in client/server communication.
   */
  uint16_t clientId; 

  // Everything following is the payload
protected:
  /** Provide a constructor to ensure the concrete message types initialize all Message fields
   */
  Message(uint32_t size, Type type);
};

}}}
