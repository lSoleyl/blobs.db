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
  static MessagePointer Create(client_id clientId, std::string_view remoteIp);

  static constexpr Type type = Type::ConnectionOpened;
private:
  ConnectionOpened(message_size messageSize, client_id clientId, std::string_view remoteIp);
};


std::ostream& operator<<(std::ostream& out, const ConnectionOpened& message);

}}}