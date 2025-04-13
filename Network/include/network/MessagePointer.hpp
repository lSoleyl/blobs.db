#pragma once

#include "message/Message.hpp"

#include <memory>
#include <type_traits>

namespace blobs {
namespace network {

/** Basically a std::unique_ptr<message::Message> with some added utility methods
 */
template<typename T = message::Message>
class MessagePointer_T {
public:
  MessagePointer_T() : ptr(nullptr) {}
  MessagePointer_T(T* ptr) : ptr(ptr) {}

  
  // We need access to the internal pointer of other typed message pointers for efficient implementation
  template<typename Other> friend class MessagePointer_T;

  ~MessagePointer_T() {
    if (ptr) {
      delete ptr;
    }
  }

  // No copy construction/assignment
  MessagePointer_T(const MessagePointer_T&) = delete;
  MessagePointer_T& operator=(const MessagePointer_T&) = delete;

  /** Move construction from another message pointer of optionally derived type
   */
  template<typename Derived>
  MessagePointer_T(MessagePointer_T<Derived>&& other, std::enable_if_t<std::is_base_of_v<T, Derived>, int> = 0) : ptr(other.ptr) {
    other.ptr = nullptr;
  }

  template<typename Derived>
  std::enable_if_t<std::is_base_of_v<T, Derived>> Reset(Derived* newPtr) {
    if (ptr) {
      delete ptr;
    }
    ptr = newPtr;
  }

  /** Move assignment from another message pointer of optionally derived type
   */
  template<typename Derived>
  std::enable_if_t<std::is_base_of_v<T, Derived>, MessagePointer_T&> operator=(MessagePointer_T<Derived>&& other) {
    Reset<Derived>(other.ptr);
    other.ptr = nullptr;
    return *this;
  }
  

  // Pointer semantics
  T* operator->() const { return ptr; }
  T& operator*() const { return *ptr; }
  explicit operator bool() const { return ptr != nullptr; }


  /** Check whether the message is of the given type (without performing a nullptr check)
   */ 
  template<typename MessageType>
  bool Is() const { 
    return ptr->type == MessageType::type; 
  }

  /** Get the message if it is of the given type (returns nullptr otherwise)
   */
  template<typename MessageType = T>
  MessageType* Get() const {
    if constexpr (std::is_same_v<MessageType, T>) {
      // No type check performed if Get() called with the message's static type
      return ptr;
    } else {
      return Is<MessageType>() ? static_cast<MessageType*>(ptr) : nullptr;
    }
  }

  /** Casts to the target message type without performing a previous type check
   */
  template<typename MessageType>
  MessageType& As() const {
    return *static_cast<MessageType*>(ptr);
  }

  /** Casts the message smart pointer into another type without first performing a type check.
   *  This object will afterwards be deinitialized
   */
  template<typename MessageType>
  MessagePointer_T<MessageType> Cast() {
    auto released = static_cast<MessageType*>(ptr);
    ptr = nullptr;
    return MessagePointer_T<MessageType>(released);
  }

private:
  T* ptr;
};

using MessagePointer = MessagePointer_T<>;


}}

