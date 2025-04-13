#include <network/message/ConnectionOpened.hpp>

namespace blobs {
namespace network {
namespace message {


ConnectionOpened::ConnectionOpened(uint32_t messageSize, uint16_t clientId, std::string_view remoteIp) : Message(messageSize, ConnectionOpened::type) {
  this->clientId = clientId;
  std::copy_n(remoteIp.data(), remoteIp.size(), reinterpret_cast<char*>(this) + sizeof(ConnectionOpened));
}


std::string_view ConnectionOpened::GetRemoteIp() const {
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(ConnectionOpened), size - sizeof(ConnectionOpened));
}

MessagePointer ConnectionOpened::Create(uint16_t clientId, std::string_view remoteIp) {
  auto messageSize = static_cast<uint32_t>(sizeof(ConnectionOpened) + remoteIp.size());
  return MessagePointer(new (new char[messageSize]) ConnectionOpened(messageSize, clientId, remoteIp));
}


}
}
}