#define BLOBS_EXPORT __declspec(dllexport)

#include <blobs/Initialization.hpp>
#include <blobs/Session.hpp>
#include <blobs/Exception.hpp>
#include <network/SocketFactory.hpp>

namespace blobs {

void Initialize() {
  // Activate the network socket factory
  network::SocketFactory::Use();

  // Initialize the global session
  Session::Initialize();
}

void Initialize(const blobs::Configuration&) {
  // Client-only version performs regular initialization here
  Initialize();
}

void Shutdown() {
  TODO("Should we kill all clients here?");

  // Shutdown the global session
  Session::Shutdown();
}


// Now the dummy Configuration implementation to avoid linker errors when using Config in regular client dll
Configuration& Configuration::Get() {
  static Configuration configInstance;
  return configInstance;
}

/** Implementation of the exported blobs::Configuration class (not blobs::server::Configuration!)
 */
Configuration::Configuration() {}
Configuration::~Configuration() {}


Configuration& Configuration::LogLevel(blobs::LogLevel logLevel) {
  return *this;
}

Configuration& Configuration::LogFile(const char* u8LogFilePath) {
  return *this;
}

Configuration& Configuration::LogFile(const wchar_t* u16LogFilePath) {
  return *this;
}

Configuration& Configuration::DbRootDir(const char* u8RootDir) {
  return *this;
}

Configuration& Configuration::DbRootDir(const wchar_t* u16RootDir) {
  return *this;
}

Configuration& Configuration::NoDbRootDir() {
  return *this;
}

Configuration& Configuration::Port(int portNo) {
  return *this;
}

Configuration& Configuration::DatabaseCloseDelay(std::chrono::milliseconds closeDelayMs) {
  return *this;
}

}