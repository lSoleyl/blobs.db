#pragma once

#include <common/BlobLocation.hpp>

namespace blobs {
namespace server {

/** This class represents a single lock held by a client
 */
class Lock {
public:
  Lock(const BlobLocation& location);
  Lock(Lock&& other) = default;
  Lock& operator=(Lock&& other) = default;

  bool operator<(const Lock& other) const;
  bool operator<(const BlobLocation& location) const;

  /** Releases this lock for the specified client
   * 
   * @return true if this lock is not being held afterwards anymore
   */
  bool Release(client_id client);

  BlobLocation location;
  std::vector<client_id> read; // all clients holding read locks
  std::optional<client_id> write; // the only client holding a write lock (if any)


private:
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

/** Needed for transparent lookup of in std::set<Lock> to work
 */
bool operator<(const BlobLocation& location, const Lock& lock);

}}