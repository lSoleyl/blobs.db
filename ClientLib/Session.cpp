#include "pch.hpp"

#include <blobs/Session.hpp>
#include <blobs/Exception.hpp>
#include <blobs/Transaction.hpp>
#include <internal/Network.hpp>
#include <internal/TransactionsState.hpp>
#include <internal/DatabasesState.hpp>

#include <mutex>

namespace blobs {

Session::Handle Session::globalSession;


struct Session::State {
  /** The state constructor will initialize the refCount to 1 and the Handle's Session* constructor will not increment it
   */
  State() : refCount(1) {}

  std::atomic<int> refCount;
  std::mutex mutex;
  std::atomic<std::thread::id> lockedBy;
  internal::Network network;
  internal::TransactionsState transactions; // Used by Transaction class
  std::map<connection_id, internal::DatabasesState> databases; // used by Database class
};

Session::Handle::Handle() noexcept : session(nullptr) {}
Session::Handle::Handle(Session* session) noexcept : session(session) {}

Session::Handle::Handle(const Handle& other) : session(other.session) {
  if (other.session) {
    session->Remember();
  }
}

Session::Handle::Handle(Handle&& other) noexcept : session(other.session) {
  other.session = nullptr;
}

Session::Handle::~Handle() {
  if (session) {
    session->Release(); // This may trigger the destruction of the referenced session
  }
}

Session::Handle& Session::Handle::operator=(const Handle& other) {
  this->~Handle();
  new (this) Handle(other);
  return *this;
}

Session::Handle& Session::Handle::operator=(Handle&& other) {
  this->~Handle();
  new (this) Handle(std::move(other));
  return *this;
}

bool Session::Handle::Reset() {
  if (session) {
    auto result = session->Release();
    session = nullptr;
    return result;
  }

  return false;
}



Session::LockHandle::LockHandle() : session(nullptr) {}

Session::LockHandle::LockHandle(Session* session) : session(session) {
  if (session->state->lockedBy.load(std::memory_order::memory_order_acquire) == std::this_thread::get_id()) {
    // Lock is already held by this thread, DO NOT re-acquire it (so we can avoid using a std::recursive_mutex)
    // and still support reentrant calls of Lock() (eg. when Database::CreateCluster() calls Database::CreateBlob())
    this->session = nullptr;
  } else {
    // Lock is either not held, or held by another thread -> lock it for this thread
    session->state->mutex.lock();
    session->state->lockedBy.store(std::this_thread::get_id(), std::memory_order::memory_order_release);
  }
}

Session::LockHandle::~LockHandle() {
  Unlock();
}

Session::LockHandle::LockHandle(LockHandle&& other) : session(other.session) {
  other.session = nullptr;
}

Session::LockHandle& Session::LockHandle::operator=(LockHandle&& other) {
  this->~LockHandle();
  new (this) LockHandle(std::move(other));
  return *this;
}


void Session::LockHandle::Unlock() {
  if (session) {
    session->state->lockedBy.store(std::thread::id(), std::memory_order::memory_order_release); // Reset to default id (no thread holds the mutex)
    session->state->mutex.unlock();
    session = nullptr;
  }
}


Session::Session() : state(new State) {}
Session::~Session() {}

Session::Handle Session::Create() {
  return Handle(new Session);
}

Session::LockHandle Session::Lock() {
  return LockHandle(this);
}

bool Session::OwnsLock() const {
  return state->lockedBy.load(std::memory_order::memory_order_acquire) == std::this_thread::get_id();
}

internal::Network& Session::Network() {
  return state->network;
}

internal::TransactionsState& Session::Transactions() {
  return state->transactions;
}

internal::DatabasesState& Session::Databases(connection_id connectionId) {
  return state->databases[connectionId];
}

void Session::EraseDatabase(connection_id connectionId, database_id databaseId) {
  auto& openedDatabases = state->databases[connectionId].openedDatabases;
  openedDatabases.erase(databaseId);
  if (openedDatabases.empty()) {
    // The last database for this connection id has been closed, remove this entry from the databases state
    state->databases.erase(connectionId);
  }
}

Session::Handle Session::GetGlobalSession() {
  if (!globalSession) {
    // Perform this check on each access to the global session as forgetting blobs::Initialize() would otherwise
    // cause a nullptr exception deeper down.
    throw Exception("blobs::Initialize() not called before attempt to access global session!");
  }
  return globalSession;
}


void Session::Initialize() {
  if (globalSession) {
    throw Exception("blobs::Initialize() called twice!");
  }

  globalSession = Create();
}

void Session::Shutdown() {
  // Shutdown the global session
  if (globalSession && !globalSession.Reset()) {
    throw Exception("blobs::Shutdown() incomplete, because the global session is still being used somewhere!");
  }

  TODO("What about all other sessions?");
}

void Session::Remember() {
  ++state->refCount;
}

bool Session::Release() {
  if (--state->refCount == 0) {
    // Last reference has been released
    delete this;
    return true;
  }

  return false;
}


}