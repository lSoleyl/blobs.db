#include <network/message/ConnectionClosed.hpp>

namespace blobs {
namespace network {
namespace message {


ConnectionClosed::ConnectionClosed(client_id clientId) : Message(sizeof(ConnectionClosed), ConnectionClosed::type) {
  this->clientId = clientId;
}

MessagePointer ConnectionClosed::Create(client_id clientId) {
  return MessagePointer(new ConnectionClosed(clientId));
}


}}}