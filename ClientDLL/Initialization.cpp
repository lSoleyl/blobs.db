#define BLOBS_EXPORT __declspec(dllexport)

#include <blobs/Initialization.hpp>
#include <blobs/Session.hpp>
#include <blobs/Exception.hpp>
#include <network/SocketFactory.hpp>



void blobs::Initialize() {
  // Activate the network socket factory
  network::SocketFactory::Use();

  // Initialize the global session
  Session::Initialize();
}

void blobs::Initialize(const char*) {
  // Client-only version performs regular initialization here
  blobs::Initialize();
}


void blobs::InitializeServerLogging(LogLevel level, const wchar_t* filePath) {
  // Nothing to do, this is the client-only version
}

void blobs::Shutdown() {
  TODO("Should we kill all clients here?");

  // Shutdown the global session
  Session::Shutdown();
}

