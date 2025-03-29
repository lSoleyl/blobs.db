#pragma once

#include <cstdint>

namespace blobs {
namespace network {
namespace message {

/** The type of network message
 */
enum class Type : uint8_t {
  OpenDB,
};


/** Format for a generic network message, which all messages have to follow
 */
struct Message {
  uint32_t size; // total length of the message in bytes. This includes this header and the content bytes
  Type type;     // kind/class of message

  // Everything following is the payload
};

}}}
