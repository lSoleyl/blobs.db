#pragma once

#include <iostream>
#include "../ClientLib/include/blobs/Initialization.hpp"

// Generic logging macro, which will log into the currently configured log stream if the log level is satisfied.
// The log is wrapped inside a do {} while(false) to enforce the semicolon after the log macro.
#define BLOBS_LOG(level, message) do { if (blobs::server::logging::GetLevel() >= level) { blobs::server::logging::LogEvent(level).Stream() << message; } } while (false)

#define BLOBS_LOG_ERROR(message) BLOBS_LOG(blobs::server::logging::Level::ERROR_LEVEL, message)
#define BLOBS_LOG_WARN(message)  BLOBS_LOG(blobs::server::logging::Level::WARN_LEVEL, message)
#define BLOBS_LOG_INFO(message)  BLOBS_LOG(blobs::server::logging::Level::INFO_LEVEL, message)
#define BLOBS_LOG_DEBUG(message) BLOBS_LOG(blobs::server::logging::Level::DEBUG_LEVEL, message)


namespace blobs::server::logging {
  using Level = blobs::LogLevel;

  /** Initializes the logging system with the default configuration (which leaves logging disabled)
   */
  void Initialize();

  /** Initializes the logging system with the given configuration.
   *  If console logging is enabled (no log file specified) then the console will be set to UTF-8 output mode and
   *  will enable buffering for std::cout for faster console output.
   * 
   *  If a log file has been specified as log output then the file is created/truncated.
   * 
   * @throw std::runtime_error if the a specified log file cannot be opened
   */
  void Initialize(const blobs::Configuration& config);



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
