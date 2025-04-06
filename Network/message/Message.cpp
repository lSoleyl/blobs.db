#include <network/message/Message.hpp>


namespace blobs {
namespace network {
namespace message {

Message::Message(uint32_t size, Type type) : size(size), type(type), clientId(0) {}

}}}
