#pragma once

#include <utility>

namespace blobs {
namespace network {

template<typename HandleType>
struct ResourceDefinition {
  static HandleType NullHandle();
  static void ReleaseHandle(HandleType handle);
};




/** A generic network resource management class correctly releasing its managed resource once it falls out of scope
 */
template<typename T, typename Definition = ResourceDefinition<T>>
class Resource {
public:
  /** Initialize to an invalid resource
   */
  Resource() : handle(NullHandle()) {}

  /** Start managing the given resource
   */
  Resource(T handle) : handle(handle) {}

  /** Resource can be move constructed
   */
  Resource(Resource&& other) : handle(other.handle) {
    other.handle = NullHandle();
  }

  /** Resource can be move assinged
   */
  Resource& operator=(Resource&& other) {
    this->~Resource();
    new (this) Resource(std::move(other));
    return *this;
  }

  // but not copied
  Resource(const Resource& other) = delete;
  Resource& operator=(const Resource& other) = delete;

  ~Resource() {
    if (handle != NullHandle()) {
      ReleaseHandle(handle);
    }
  }

  /** Release resource handle and reinitialize to an empty handle
   */
  void Reset() {
    *this = Resource();
  }

  /** Derefence to access the handle
   */
  T operator*() const {
    return handle;
  }

  /** Support for pointer access when managing pointer type handles
   */
  T operator->() const {
    return handle;
  }

  /** Converts to true in boolean contexts if the handle is initialized.
   */
  explicit operator bool() const {
    return HasHandle();
  }

  bool HasHandle() const {
    return handle != NullHandle();
  }

private:
  /** Default value of the handle type representing no handle/invalid handle
   */
  static T NullHandle() { return Definition::NullHandle(); }

  /** Release the handle (i.e. call the appropriate destruction function if this is not the NullHandle.
   */
  static void ReleaseHandle(T handle) { Definition::ReleaseHandle(handle); }

  T handle;
};






}
}
