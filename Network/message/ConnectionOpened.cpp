#include <network/message/ConnectionOpened.hpp>

namespace blobs {
namespace network {
namespace message {


ConnectionOpened::ConnectionOpened(uint32_t messageSize, uint16_t clientId) : Message(messageSize, ConnectionOpened::type) {
  this->clientId = clientId;
}


std::string_view ConnectionOpened::GetRemoteIp() const {
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(ConnectionOpened), size - sizeof(ConnectionOpened));
}

MessagePointer ConnectionOpened::Create(uint16_t clientId, std::string_view remoteIp) {
  auto messageSize = sizeof(ConnectionOpened) + remoteIp.size();

  auto memory = new char[messageSize];
  MessagePointer message(new (memory) ConnectionOpened(messageSize, clientId));
  std::copy_n(remoteIp.data(), remoteIp.size(), memory + sizeof(ConnectionOpened));
  return message;
}


}
}
}