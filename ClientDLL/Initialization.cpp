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

void blobs::Shutdown() {
  TODO("Should we kill all clients here?");

  // Shutdown the global session
  Session::Shutdown();
}

