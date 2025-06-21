#define BLOBS_EXPORT __declspec(dllexport)

#include <blobs/Initialization.hpp>
#include <network/StandaloneFactory.hpp>
#include <server/Server.hpp>

#include <thread>

// We must include doctest here to not get any linker errors when linking against ServerLib, which uses doctest
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>



class StandaloneServer {
public:
  void Start() {
    serverThread = std::thread([this]() { ServerThreadMain(); });
  }

  ~StandaloneServer() {
    TODO("Signal server to close all connections and shutdown somehow...");
    serverThread.join();
  }

private:
  void ServerThreadMain() {
    SetThreadDescription(GetCurrentThread(), L"Standalone server thread");
    serverInstance.ServerMain();
    TODO("What about error handling?");
  }

  blobs::server::Server serverInstance;
  std::thread serverThread;
};


std::unique_ptr<StandaloneServer> standaloneServer;


void blobs::Initialize() {
  // Activate the network socket factory
  network::StandaloneFactory::Use();

  // Initialize and start the local server instance
  standaloneServer = std::make_unique<StandaloneServer>();
  standaloneServer->Start();
}

void blobs::Shutdown() {
  // Wait for server thread to complete shutdown
  standaloneServer.reset();
}

