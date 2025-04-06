#include <network/message/ConnectionClosed.hpp>

namespace blobs {
namespace network {
namespace message {


ConnectionClosed::ConnectionClosed(uint16_t clientId) : Message(sizeof(ConnectionClosed), ConnectionClosed::type) {
  this->clientId = clientId;
}

MessagePointer ConnectionClosed::Create(uint16_t clientId) {
  return MessagePointer(new ConnectionClosed(clientId));
}


}}}