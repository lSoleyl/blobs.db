#pragma once

#include "..\MessagePointer.hpp"

namespace blobs {
namespace network {
namespace message {

/** This message is currently only generated internally to indicate a closed connection through the regular message queue
 */
struct ConnectionClosed : public Message {
public:
  /** Constructs a new internally used ConnectionClosed message
   */
  static MessagePointer Create(uint16_t clientId = 0);

  static constexpr Type type = Type::ConnectionClosed;
private:
  ConnectionClosed(uint16_t clientId);
};



}}}