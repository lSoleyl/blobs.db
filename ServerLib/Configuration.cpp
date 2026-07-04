#include "pch.hpp"

#include <server/Configuration.hpp>
#include <common/Encoding.hpp>

namespace blobs {


Configuration& Configuration::Get() {
  static Configuration configInstance;
  return configInstance;
}

/** Implementation of the exported blobs::Configuration class (not blobs::server::Configuration!)
 */
Configuration::Configuration() : server(new server::Configuration) {}
Configuration::~Configuration() { delete server; }


Configuration& Configuration::LogLevel(blobs::LogLevel logLevel) {
  server->logLevel = logLevel;
  return *this;
}

Configuration& Configuration::LogFile(const char* u8LogFilePath) {
  server->logFile = encoding::ToUTF16(u8LogFilePath);
  return *this;
}

Configuration& Configuration::LogFile(const wchar_t* u16LogFilePath) {
  server->logFile = u16LogFilePath;
  return *this;
}



Configuration& Configuration::DbRootDir(const char* u8RootDir) {
  server->dbRootDir = encoding::ToUTF16(u8RootDir);
  return *this;
}

Configuration& Configuration::DbRootDir(const wchar_t* u16RootDir) {
  server->dbRootDir = u16RootDir;
  return *this;
}

Configuration& Configuration::NoDbRootDir() {
  server->dbRootDir.reset();
  return *this;
}

Configuration& Configuration::Port(int portNo) {
  server->port = portNo;
  return *this;
}

Configuration& Configuration::DatabaseCloseDelay(std::chrono::milliseconds closeDelayMs) {
  server->databaseCloseDelay = closeDelayMs;
  return *this;
}

}