#pragma once

#include "..\MessagePointer.hpp"

#include <string_view>

namespace blobs {
namespace network {
namespace message {

/** This message is currently only generated internally to indicate a new client connection to the server
 */
struct ConnectionOpened : public Message {
public:

  /** Retrieves the stored remote ip address (as string) from this message
   */
  std::string_view GetRemoteIp() const;

  /** Constructs a new internally used ConnectionOpened message
   */
  static MessagePointer Create(uint16_t clientId, std::string_view remoteIp);

  static constexpr Type type = Type::ConnectionOpened;
private:
  ConnectionOpened(uint32_t messageSize, uint16_t clientId, std::string_view remoteIp);
};



}
}
}