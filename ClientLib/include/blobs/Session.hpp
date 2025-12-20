#pragma once

#include <mutex>

namespace blobs {


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
  public:
    BLOBS_EXPORT Handle() noexcept;
    BLOBS_EXPORT Handle(const Handle& other);
    BLOBS_EXPORT Handle(Handle&& other) noexcept;
    BLOBS_EXPORT ~Handle();

    BLOBS_EXPORT Handle& operator=(const Handle& other);
    BLOBS_EXPORT Handle& operator=(Handle&& other);
    
    /** Only used inside Session::Create()
     */
    Handle(Session* session);

    explicit operator bool() const { return session != nullptr; }
    Session* operator->() const { return session; }

    /** Resets the handle to an empty handle releasing the session pointer
     * 
     * @return true if this was the last handle to the session and the session has been deleted, false otherwise
     */
    BLOBS_EXPORT bool Reset();


  private:
    Session* session;
  };


  /** Create a new session and return a session handle to it
   */
  BLOBS_EXPORT static Handle Create();


  /** Internally used method to acquire the session's lock for cross thread synchronization
   */
  std::unique_lock<std::mutex> Lock();

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