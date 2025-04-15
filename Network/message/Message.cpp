#include <network/message/Message.hpp>


namespace blobs {
namespace network {
namespace message {

Message::Message(message_size size, Type type) : size(size), type(type), clientId(0) {}

}}}
