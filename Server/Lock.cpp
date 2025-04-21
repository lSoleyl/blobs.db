#include "pch.hpp"
#include "Lock.hpp"

namespace blobs {
namespace server {

Lock::Lock(const BlobLocation& location) : location(location) {}

bool Lock::operator<(const Lock& other) const {
  return location < other.location;
}

bool Lock::operator<(const BlobLocation& location) const {
  return this->location < location;
}

bool Lock::Release(client_id client) {
  if (write) {
    // Fast path as only one write lock can exist at any time
    if (*write == client) {
      write.reset();
      return true;
    }
    assert(false); // This should never happen as the client should always know, which locks he holds
    return false; 
  } else {
    // Remove the client lock from the read lock holders
    auto pos = std::remove(read.begin(), read.end(), client);
    read.erase(pos, read.end());
    return read.empty();
  }
}

bool operator<(const BlobLocation& location, const Lock& lock) {
  return location < lock.location;
}

}}