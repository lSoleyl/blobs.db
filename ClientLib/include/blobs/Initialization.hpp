#pragma once

#include "Config.hpp"
#include "LogLevel.hpp"

namespace blobs {

class Configuration;

/** Must be called before any other blobs function
 */
BLOBS_EXPORT void Initialize();

/** Initialization overload, which accepts a (server) configuration (see below). This configuration may be used to 
 *  configure the logging and database root for standalone client applications.
 */
BLOBS_EXPORT void Initialize(const Configuration& config);


namespace server { class Configuration; }

/** This class is used to configure the server parameters for blobs.db.
 *  Retrieve a configuration instance through Configuration::Get() and then call
 *  the modifier methods on this object and finally pass this object to blobs::Initialize() to 
 *  perform initialization with the specified configuration.
 * 
 *  As this configuration currently only contains server parameters, this is only relevant for
 *  clients linked against the standalone library of blobs.db (and for the server itself).
 * 
 *  Passing a configuration object to blobs::Initialize() in a regular client will simply ignore the specified configuration 
 *  at the moment. This may change should the configuration receive parameters relevant to a regular client.
 */
class Configuration {
public:
  /** Use this static instance getter to retireve a Configuration instance that may be passed to blobs::initialize()
   */
  BLOBS_EXPORT static Configuration& Get();

  /** @name server logging configuration
   *  Ignored on the client unless built as standalone client
   */
  //@{

  /** Sets the server logging level. Setting a log level different from OFF_LEVEL will
   *  enable logging. Logging will be performed to std::cout by default, but may be set to a file
   *  (see LogFile()). If logging is performed to std::cout, std::cout is set to UTF_8 mode and 
   *  buffering is enabled for better logging performance. This could impact client console applications.
   * 
   * @param logLevel the new log level to set
   * 
   * @default OFF_LEVEL
   */
  BLOBS_EXPORT Configuration& LogLevel(LogLevel logLevel);

  /** Sets the file to write the logs into. The file is created/truncated upon initialization and
   *  a std::runtime_error is thrown in blobs::Initialize() if the file cannot be opened for writing.
   *  
   * @param u8LogFilePath the UTF-8 encoded log file path
   */
  BLOBS_EXPORT Configuration& LogFile(const char* u8LogFilePath);

  /** Sets the file to write the logs into. The file is created/truncated upon initialization and
   *  a std::runtime_error is thrown in blobs::Initialize() if the file cannot be opened for writing.
   *
   * @param u8LogFilePath the UTF-16 encoded log file path
   */
  BLOBS_EXPORT Configuration& LogFile(const wchar_t* u16LogFilePath);

  //@}


  /** @name database root
   *  Ignored on the client unless built as standalone client
   */
  //@{

  /** Set the database server's database root directory.
   *
   * @param dbRootDir pass a UTF-8 encoded file path here to specify the database root directory.
   *                  If a relative path is specified here, then the path is resolved relative to the current working
   *                  directory of this process. (Set the log level to DEBUG to see, which database root directory is being used).
   
   * 
   * @default ".\\databases"
   */
  BLOBS_EXPORT Configuration& DbRootDir(const char* u8RootDir);

  /** Set the database server's database root directory.
   *
   * @param dbRootDir pass a UTF-16 encoded file path here to specify the database root directory.
   *                  If a relative path is specified here, then the path is resolved relative to the current working
   *                  directory of this process. (Set the log level to DEBUG to see, which database root directory is being used).

   *
   * @default L".\\databases"
   */
  BLOBS_EXPORT Configuration& DbRootDir(const wchar_t* u16RootDir);

  /** Disables the database root directory and allows clients to open databases anywhere on the filesystem (dangerous).
   */
  BLOBS_EXPORT Configuration& NoDbRootDir();

  //@}

  /** Set the server's listen port. This configuration value has no effect in any client builds as the server does not use TCP ports
   *  for communication in standalone clients and regular clients do not start a server process.
   * 
   * @param portNo the port number to set
   * 
   * @default 8081
   */
  BLOBS_EXPORT Configuration& Port(int portNo);
  //@}

  Configuration();
  ~Configuration();
  server::Configuration* server;
  // Maybe we will add client configuration in here too

  // Configuration objects are not copyable
  Configuration(const Configuration&) = delete;
  Configuration& operator=(const Configuration&) = delete;
};


/** This function should be called after all connections have been closed to perform some final cleanup operations
 */
BLOBS_EXPORT void Shutdown();

}