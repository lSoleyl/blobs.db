#pragma once

#include <common/BlobLocation.hpp>

namespace blobs {
namespace server {

/** This class represents a single lock held by a client
 */
class Lock {
public:
  Lock(const BlobLocation& location);

  /** Creates a new lock, which is immediately held by the given client
   */
  Lock(const BlobLocation& location, client_id client, bool writeLock);

  Lock(Lock&& other) = default;
  Lock& operator=(Lock&& other) = default;

  bool operator<(const Lock& other) const;
  bool operator<(const BlobLocation& location) const;

  /** Returns true if the client can acquire this lock with the specified access or can upgrade its access
   *  or already owns this lock with the specified access.
   */
  bool CanAcquire(client_id client, bool writeLock) const;

  /** Acquires this lock for the specified client in the given read/write mode.
   *  This method assumes that the lock can be acquired and performs no further checks.
   *  
   * @pre CanAcquire(client,writeLock) == true
   */
  void Acquire(client_id client, bool writeLock);

  /** Releases this lock for the specified client
   * 
   * @return true if this lock is not being held afterwards anymore
   */
  bool Release(client_id client);


  /** Returns true if the specified client holds a write lock
   */
  bool OwnsWriteLock(client_id client) const;
  
private:
  const BlobLocation location;
  std::vector<client_id> read; // all clients holding read locks
  std::optional<client_id> write; // the only client holding a write lock (if any)

  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

  friend bool operator<(const BlobLocation& location, const Lock& lock);
};

/** Needed for transparent lookup of in std::set<Lock> to work
 */
bool operator<(const BlobLocation& location, const Lock& lock);

}}