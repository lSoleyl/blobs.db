#pragma once

#include "../ClientLib/include/blobs/Initialization.hpp"

namespace blobs::server {

/** This class is used by the blobs::Configuration class
 */
class Configuration {
public:
  /** Database root directory (the directory where all database are looked up in).
   *  Relative paths are considered relative to the database root.
   *  Paths outside the database root cannot be opened.
   */
  std::optional<std::wstring> dbRootDir = L".\\databases";

  /** The server port to listen incomming client connections.
   */
  int port = 8108;

  /** The configured log level for server logging.
   *  OFF disables logging entirely
   */
  blobs::LogLevel logLevel = blobs::LogLevel::OFF_LEVEL;

  /** The log file to log into. If none is set then and log level is not OFF then
   *  logging will be performed to std::cout
   */
  std::optional<std::wstring> logFile;

  /** Optional duration to delay acutally closing a database by.
   *  default 0 = no delay
   */
  std::chrono::milliseconds databaseCloseDelay;
};

}

