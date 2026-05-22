#pragma once

#include "Config.hpp"

#include <memory>


namespace blobs {

namespace internal {
  class Network;
  class TransactionsState;
  class DatabasesState;
}

/** The session represents a client instance, inside the client. It allows one process to open multiple connections to the
 *  same database and operate in separate transactions. To the server this looks like multiple clients connecting from the same IP.
 * 
 *  Each process always has a global session, which is created in blobs::Initialize() and lives until blobs::Shutdown() is called.
 *  Databases and Transactions are associated with the session in which they were created.
 *  If multiple threads attempt to work on the same session, then their access to the session will by serialized. (Sessions are thread-safe)
 */
class Session {
public:
  /** Session::Handle is the only type that will be returned by static Session creation functions.
   *  The handle is movable and copyable and will ensure that the session stays alive as long as at least one 
   *  handle to the session exists and the session is deleted once the last handle is dropped. (And all Databases for that session have been closed)
   * 
   *  The session and its ref counter are thread safe, the Session::Handle itself is not. Do not share the same Session::Handle object between threads as
   *  the handle's move constructor and destructor perform no thread synchronization (to avoid the mostly unneeded overhead). To use the same session across multiple
   *  threads, simply copy the handle, this is cheap and thread safe.
   */
  class Handle {
    friend class Session;
  public:
    BLOBS_EXPORT Handle() noexcept;
    BLOBS_EXPORT Handle(const Handle& other);
    BLOBS_EXPORT Handle(Handle&& other) noexcept;
    BLOBS_EXPORT ~Handle();

    BLOBS_EXPORT Handle& operator=(const Handle& other);
    BLOBS_EXPORT Handle& operator=(Handle&& other);
    
    explicit operator bool() const { return session != nullptr; }
    Session* operator->() const { return session; }

    /** Resets the handle to an empty handle releasing the session pointer
     * 
     * @return true if this was the last handle to the session and the session has been deleted, false otherwise
     */
    BLOBS_EXPORT bool Reset();


  private:
    /** Only used inside Session::Create()
     */
    Handle(Session* session) noexcept;

    Session* session;
  };


  /** Create a new session and return a session handle to it
   */
  BLOBS_EXPORT static Handle Create();

  /** An RAII lock implementation class similar to std::unique_lock. 
   *  The main reason for using this class instead of std::unique_lock is to be able to 
   *  store inside the Session whether the session is actually locked or not (for debugging purposes)
   */
  class LockHandle {
    friend class Session;
    public:
      LockHandle();
      ~LockHandle();

      // Do NOT use the move operators to move lock ownership across threads, this does not work
      LockHandle(LockHandle&& other);
      LockHandle& operator=(LockHandle&& other);

      /** Returns true if this lock currently holds a locked session
       */
      explicit operator bool() const { return session != nullptr; }

      /** If the lock was held, then calling this method will release it
       */
      void Unlock();

    private:
      /** Locks the session's mutex and marks the session as locked by this thread.
       *  This was done mainly for debugging purposes, but is now used to avoid using a std::recursive_mutex and still
       *  support locking a session while already holding a lock.
       */
      LockHandle(Session* session);

      // Not copyable
      LockHandle(const LockHandle&) = delete;
      LockHandle& operator=(const LockHandle&) = delete;

      Session* session;
  };



  /** Internally used method to acquire the session's lock for cross thread synchronization
   *  The lock may be acquired recursively in which case the inner Lock() calls will simply return an empty LockHandle
   *  and NOT perform any locking/unlocking of the mutex.
   */
  LockHandle Lock();

  /** Returns true if this session is locked and the current thread owns the lock (acquired it)
   *  This is mainly used for debugging/checking internal invariants
   */
  bool OwnsLock() const;

  /** Access to the session's connection manager. Only call this AFTER locking the session through Lock()!
   */
  internal::Network& Network();

  /** Access to the session's transaction state. Only call this AFTER locking the session through Lock()!
   */
  internal::TransactionsState& Transactions();

  /** Access to the session's database state for the given connection id. 
   *  Only call this AFTER locking the session through Lock()!
   */
  internal::DatabasesState& Databases(connection_id connectionId);

  /** Removes a database (that is about to be deleted) from the session's database map
   */
  void EraseDatabase(connection_id connectionId, database_id databaseId);

  /** Returns a copy of the global session handle.
   */
  BLOBS_EXPORT static Handle GetGlobalSession();


  /** Initializes the global session handle
   */
  static void Initialize();

  /** Releases the global session handle
   */
  static void Shutdown();

private:
  /** A session can only be created using Session::Create()
   */
  Session();

  /** A session can only be deleted by reducing its ref count to 0 by dropping the last Session::Handle
   */
  ~Session();


  /** Internally used by Session::Handle() to increment the session's internal reference count
   */
  void Remember();

  /** Internally used by Session::Handle() to release the session's internal reference count
   * 
   * @returns true if the session has been deleted through this call, false otherwise
   */
  bool Release();
  

  struct State;
  std::unique_ptr<State> state; // pimpl to avoid exposing all session internals to the client lib

  static Handle globalSession; // the global session handle (created in blobs::Initialize(), released in blobs::Shutdown())
};



}