#pragma once

#include <common/BlobLocation.hpp>

namespace blobs {
namespace server {

/** This interface must be implemented by callers of CanAcquire() and Acquire() to correctly implement sticky lock behavior
 *  i.e. a lock can be revoked if the client holding the lock is currently not inside a transaction (it is a sticky lock, not an active lock).
 */
class StickyLockInterface {
public:
  /** Should return true if the client is not currently inside a transaction and thus a sticky lock 
   *  can be revoked from it
   */
  virtual bool CanRevokeStickyLock(client_id id) const = 0;

  /** Revokes a sticky lock from a currently inactive client
   */
  virtual void RevokeStickyLock(client_id id, const BlobLocation& location) = 0;
};

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
   * 
   * @param client the id of the client requesting a lock
   * @param writeLock true = requesting a write lock / false = requesting a read lock
   * @param stickyLocks the structure holding information about which clients are currently inside a transaction.
   */
  bool CanAcquire(client_id client, bool writeLock, const StickyLockInterface& stickyLocks) const;


  /** Performs the same check as CanAcquire, but instead of simply returning true/false, this method instead returns
   *  all client ids, which prevent the lock's acquisition. This method is only used when checking for deadlocks.
   * 
   * @param client the id of the client requesting a lock
   * @param writeLock true = requesting a write lock / false = requesting a read lock
   * @param stickyLocks the structure holding information about which clients are currently inside a transaction.
   * 
   * @return list of all clients, which prevent this lock's acquisition
   */
  std::vector<client_id> CollectConflictingClients(client_id client, bool writeLock, const StickyLockInterface& stickyLocks) const;

  /** Acquires this lock for the specified client in the given read/write mode.
   *  This method assumes that the lock can be acquired and performs no further checks.
   *  This method also revokes other client's sticky locks if necessary.
   *  
   * @pre CanAcquire(client,writeLock) == true
   * 
   * @param client the client to acquire the lock for
   * @param writeLock whether to acquire a write (true) or read (false) lock
   * @param stickyLocks the interface, which is used to revoke sticky locks from inactive clients
   */
  void Acquire(client_id client, bool writeLock, StickyLockInterface& stickyLocks);

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