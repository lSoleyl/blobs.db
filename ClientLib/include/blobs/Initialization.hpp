#pragma once

#include "Config.hpp"


namespace blobs {

/** Must be called before any other blobs function
 */
BLOBS_EXPORT void Initialize();

/** Initialization overload, which may be used in the standalone version of the blobs.db client to specify the 
 *  database root directory - i.e. the directory where all file databases will reside an and which will be used to 
 *  resolve all relative file database paths.
 * 
 *  The default database root directory is ".\\databases". This is the root directory that is being used, when just
 *  callign Initialize().
 * 
 * @param dbRootDir pass a utf8 encoded file path here to specify the database root directory.
 *                  If a relative path is specified here, then the path is resolved relative to the current working
 *                  directory of this process. (Set the log level to DEBUG to see, which database root directory is being used).
 *                  Passing nullptr will disable the database root directory and allow clients to open databases anywhere on the filesystem.
 */
BLOBS_EXPORT void Initialize(const char* dbRootDir);


enum class LogLevel : int {
  OFF_LEVEL,
  ERROR_LEVEL,
  WARN_LEVEL,
  INFO_LEVEL,
  DEBUG_LEVEL
};

/** Initializes the server logging to the specified log level and log file.
 *  This function should only be called when running the standalone version of the blobs.db client and
 *  server logs are needed to help track down some issue.
 * 
 *  This function can be called BEFORE blobs::Initialize() to also catch logs written by blobs::Initialize().
 * 
 *  In standalone mode the server by default does not log anything at all.
 *  When logging to std::cout (filePath == nullptr) the logging system will change the console output mode
 *  to UTF-8 and set the console output into buffered output mode, so be aware of that.
 * 
 * @param level the log level to use 
 * @param filePath the file to log to. Pass nullptr to log into std::cout.
 * 
 * @throws std::runtime_error if the specified file cannot be opened
 */
BLOBS_EXPORT void InitializeServerLogging(LogLevel level, const wchar_t* filePath = nullptr);


/** This function should be called after all connections have been closed to perform some final cleanup operations
 */
BLOBS_EXPORT void Shutdown();

}