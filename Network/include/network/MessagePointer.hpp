#pragma once

#include "message/Message.hpp"

#include <memory>

namespace blobs {
namespace network {

/** std::unique_ptr<message::Message> with some added utility methods
 */
class MessagePointer : public std::unique_ptr<message::Message> {
public:
  // Inherit all base class constructors
  using std::unique_ptr<message::Message>::unique_ptr;

  /** Check whether the message is of the given type (without performing a nullptr check)
   */ 
  template<typename MessageType>
  bool Is() const { 
    return get()->type == MessageType::type; 
  }

  /** Get the message if it is of the given type (returns nullptr otherwise)
   */
  template<typename MessageType>
  MessageType* Get() const { 
    return Is<MessageType>() ? static_cast<MessageType*>(get()) : nullptr;
  }

  /** Casts to the target message type without performing a previous type check
   */
  template<typename MessageType>
  MessageType& As() const {
    return *static_cast<MessageType*>(get());
  }
};

}}

