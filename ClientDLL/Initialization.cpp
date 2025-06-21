#define BLOBS_EXPORT __declspec(dllexport)

#include <blobs/Initialization.hpp>
#include <network/SocketFactory.hpp>



void blobs::Initialize() {
  // Activate the network socket factory
  network::SocketFactory::Use();
}

void blobs::Shutdown() {
  TODO("Should we kill all clients here?");
  // Nothing to do for now...
}

