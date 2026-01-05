#pragma once

#include <iostream>

// Generic logging macro, which will log into the currently configured log stream if the log level is satisfied.
// The log is wrapped inside a do {} while(false) to enforce the semicolon after the log macro.
#define BLOBS_LOG(level, message) do { if (blobs::server::logging::GetLevel() >= level) { blobs::server::logging::LogEvent(level).Stream() << message; } } while (false)

#define BLOBS_LOG_ERROR(message) BLOBS_LOG(blobs::server::logging::Level::ERROR_LEVEL, message)
#define BLOBS_LOG_WARN(message)  BLOBS_LOG(blobs::server::logging::Level::WARN_LEVEL, message)
#define BLOBS_LOG_INFO(message)  BLOBS_LOG(blobs::server::logging::Level::INFO_LEVEL, message)
#define BLOBS_LOG_DEBUG(message) BLOBS_LOG(blobs::server::logging::Level::DEBUG_LEVEL, message)


namespace blobs::server::logging {

  // All log levels have the _LEVEL suffix to avoid name collisions with macros like ERROR (defined in windows.h)
  enum class Level : int {
    OFF_LEVEL,
    ERROR_LEVEL,
    WARN_LEVEL,
    INFO_LEVEL,
    DEBUG_LEVEL
  };

  /** Initializes logging system to use console output for logging. This will
   *  Enable UTF-8 console output and enable buffering for std::cout for faster console output.
   */
  void Initialize(Level level);

  /** Initializes the logging system to use the given log file for logging. 
   *  Throws a runtime_error if file cannot be created/opened.
   */
  void Initialize(Level level, const wchar_t* filePath);

  /** Returns the currently configured log level (default = OFF)
   */
  Level GetLevel();

  /** Returns the stream, which logs should be written to (default = std::cout)
   */
  std::ostream& GetStream();
  
  
  /** Shutdown the logging system. This will mainly close any opened log file.
   */
  void Shutdown();


  /** Upon creation writes the timestamp and loglevel into the log stream and upon deletion writes a newline into it.
   */
  class LogEvent {
    public:
      LogEvent(Level level);
      ~LogEvent();
      std::ostream& Stream() const;
  };
}
