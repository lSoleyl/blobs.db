#include "pch.hpp"

#include <blobs/Session.hpp>
#include <blobs/Exception.hpp>

#include <mutex>

namespace blobs {

Session::Handle Session::globalSession;


struct Session::State {
  /** The state constructor will initialize the refCount to 1 and the Handle's Session* constructor will not increment it
   */
  State() : refCount(1) {}

  std::atomic<int> refCount;
  std::mutex mutex; // Do we need a recursive one?
};

Session::Handle::Handle() noexcept : session(nullptr) {}
Session::Handle::Handle(Session* session) : session(session) {}

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


Session::Session() : state(new State) {}
Session::~Session() {}

Session::Handle Session::Create() {
  return Handle(new Session);
}

std::unique_lock<std::mutex> Session::Lock() {
  return std::unique_lock<std::mutex>(state->mutex);
}

Session::Handle Session::GetGlobalSession() {
  return globalSession;
}


void Session::Initialize() {
  if (globalSession) {
    throw Exception("Session::Initialize() called twice!");
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