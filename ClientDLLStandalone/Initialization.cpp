#define BLOBS_EXPORT __declspec(dllexport)

#include <blobs/Initialization.hpp>
#include <blobs/Session.hpp>
#include <network/StandaloneFactory.hpp>
#include <server/Server.hpp>
#include <server/Logging.hpp>
#include <server/Configuration.hpp>
#include <common/Encoding.hpp>

#include <thread>
#include <cassert>

// We must include doctest here to not get any linker errors when linking against ServerLib, which uses doctest
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>



class StandaloneServer {
public:
  StandaloneServer(const blobs::Configuration& config) : serverInstance(config) {}


  void Start() {
    serverThread = std::thread([this]() { ServerThreadMain(); });
  }

  ~StandaloneServer() {
    serverInstance.BeginShutdown();
    if (serverThread.get_id() != std::this_thread::get_id()) {
      serverThread.join();
    } else {
      // If this code is being run from inside the Standalone server thread, then the main thread must have 
      // exited BEFORE calling blobs::Shutdown()!
      assert(!"Process exited without calling blobs::Shutdown()!");
    }
  }


  static std::unique_ptr<StandaloneServer> instance;

private:
  void ServerThreadMain() {
    SetThreadDescription(GetCurrentThread(), L"blobs.db standalone server thread");
    serverInstance.ServerMain();
    TODO("What about error handling?");
  }

  blobs::server::Server serverInstance;
  std::thread serverThread;
};

std::unique_ptr<StandaloneServer> StandaloneServer::instance;


void blobs::Initialize() {
  Initialize(Configuration());
}


void blobs::Initialize(const Configuration& config) {
  server::logging::Initialize(config);
  
  // Activate the network socket factory
  network::StandaloneFactory::Use();

  // Initialize and start the local server instance
  StandaloneServer::instance = std::make_unique<StandaloneServer>(config);
  StandaloneServer::instance->Start();

  // Initialize the global session
  Session::Initialize();
}


void blobs::Shutdown() {
  // Wait for server thread to complete shutdown
  StandaloneServer::instance.reset();

  // Shutdown the global session
  Session::Shutdown();

  // Shutdown logging system (if initialized - to close any potentially opened log file)
  server::logging::Shutdown();
}

