#include <network/message/NetworkException.hpp>

namespace blobs {
namespace network {
namespace message {


NetworkException::NetworkException(message_size messageSize, std::string_view exceptionMessage) : Message(messageSize, NetworkException::type) {
  std::copy_n(exceptionMessage.data(), exceptionMessage.size(), reinterpret_cast<char*>(this) + sizeof(NetworkException));
}

std::string_view NetworkException::GetExceptionMessage() const {
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(NetworkException), size - sizeof(NetworkException));
}

MessagePointer NetworkException::Create(std::string_view exceptionMessage) {
  auto messageSize = static_cast<message_size>(sizeof(NetworkException) + exceptionMessage.size());
  return MessagePointer(new (new char[messageSize]) NetworkException(messageSize, exceptionMessage));
}





}}}