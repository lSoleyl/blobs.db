#pragma once

#include "..\MessagePointer.hpp"
#include <string_view>

namespace blobs {
namespace network {
namespace message {

/** Internal message used to communicate a network::exception form the network thread back to the main thread.
 */
struct NetworkException : public Message {
  /** The exception message string is simply allocated in the memory following the message itself
   */
  std::string_view GetExceptionMessage() const;

  /** Encodes the exception message into a sufficiently large allocated message object
   */
  static MessagePointer Create(std::string_view exceptionMessage);
  
  static constexpr Type type = Type::NetworkException;
private:
  // private constructor -> call Create instead
  NetworkException(message_size messageSize, std::string_view exceptionMessage);

};



}}}