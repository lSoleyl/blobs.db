#include "pch.hpp"
#include "include/server/Logging.hpp"

#include <Windows.h>

#include <fstream>
#include <chrono>
#include <iomanip>


namespace blobs::server::logging {


static Level logLevel = Level::OFF_LEVEL;
static std::ofstream logFile;
static std::ostream* logStream = nullptr;

void Initialize(Level level) {
  // Enable unicode console output
  SetConsoleOutputCP(CP_UTF8);

  // Enable buffering on stdio for faster writes... It also results in us having to flush the output using std::endl
  setvbuf(stdout, nullptr, _IOFBF, 1000);

  logLevel = level;
  logStream = &std::cout;
}

void Initialize(Level level, const wchar_t* filePath) {
  logFile.open(filePath);
  if (!logFile) {
    throw std::runtime_error("failed to open log file during initialization of logging system");
  }

  logLevel = level;
  logStream = &logFile;
}

Level GetLevel() {
  return logLevel;
}

std::ostream& GetStream() {
  return *logStream;
}

void SetLogFile(const wchar_t* filePath) {
  logFile.open(filePath);
  if (logFile) {
    // Set logging output stream to the file
    logStream = &logFile;
  }
}

void Shutdown() {
  if (logFile) {
    logFile.close();
  }
  // Disable all log statements after shutdown
  logLevel = Level::OFF_LEVEL;
  logStream = nullptr;
}



std::ostream& operator<<(std::ostream& out, Level level) {
  switch (level) {
    case Level::OFF_LEVEL:   return out << "[OFF]";
    case Level::ERROR_LEVEL: return out << "[ERR]";
    case Level::WARN_LEVEL:  return out << "[WRN]";
    case Level::INFO_LEVEL:  return out << "[INF]";
    case Level::DEBUG_LEVEL: return out << "[DBG]";
  }

  assert(!"Unhandled log level");
  return out << "[???]";
}


LogEvent::LogEvent(Level level) {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  tm tmBuf;
  localtime_s(&tmBuf, &tt);
  int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
  GetStream() << std::put_time(&tmBuf, "%FT%H:%M:%S.") << std::setfill('0') << std::setw(3) << ms << ' ' << level << ' ';
}

LogEvent::~LogEvent() {
  GetStream() << std::endl;
}

std::ostream& LogEvent::Stream() const {
  return GetStream();
}



}

