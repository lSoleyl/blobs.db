#include "Args.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <regex>
#include <sstream>

namespace {
constexpr size_t operator""_kB(size_t value) { return value * 1024; }
constexpr size_t operator""_MB(size_t value) { return value * 1024_kB; }
constexpr size_t operator""_GB(size_t value) { return value * 1024_MB; }
constexpr size_t operator""_TB(size_t value) { return value * 1024_GB; }
constexpr size_t operator""_PB(size_t value) { return value * 1024_TB; }
}





Args::Args() : config(blobs::Configuration::Get()) {
  // The server always starts with INFO as default log level
  config.LogLevel(blobs::LogLevel::INFO_LEVEL);
}

Args Args::Parse(int argc, const wchar_t* const* argv) {
  Args args;

  if (argc >= 2 && std::wstring_view(argv[1]) == L"--test") {
    // Run doctest -> all arguments are passed to doctest, no further argument parsing is performed
    args.runTests = true;
    args.valid = true;
    return args;
  }

  auto argPos = argv + 1;
  auto argEnd = argv + argc;

  while (argPos != argEnd) {
    std::wstring_view argName(*argPos++);

    if (argName == L"--loglevel" || argName == L"-ll") {
      if (argPos == argEnd) {
        std::wcerr << L"No log level specified for argument " << argName << "\n";
        return args; // invalid args
      }

      // Set log level
      std::wstring_view argValue(*argPos++);
      if (argValue == L"OFF") {
        args.config.LogLevel(blobs::LogLevel::OFF_LEVEL);
      } else if (argValue == L"DEBUG") {
        args.config.LogLevel(blobs::LogLevel::DEBUG_LEVEL);
      } else if (argValue == L"INFO") {
        args.config.LogLevel(blobs::LogLevel::INFO_LEVEL);
      } else if (argValue == L"WARN") {
        args.config.LogLevel(blobs::LogLevel::WARN_LEVEL);
      } else if (argValue == L"ERROR") {
        args.config.LogLevel(blobs::LogLevel::ERROR_LEVEL);
      } else {
        std::wcerr << L"Unsupported log level passed: " << argValue << "\n";
        return args; // invalid args
      }
    } else if (argName == L"--logfile" || argName == L"-lf") {
      if (argPos == argEnd) {
        std::wcerr << L"No log file specified for argument " << argName << "\n";
        return args; // invalid args
      }

      // Set log file
      args.config.LogFile(*argPos++);
    } else if (argName == L"--dbroot" || argName == L"-dr") {
      // Database root directory specified
      args.config.DbRootDir(*argPos++);

    } else if (argName == L"--nodbroot" || argName == L"-ndr") {
      // No database root, all files on the filesystem should be accessible
      args.config.NoDbRootDir();

    } else if (argName == L"--closedelay" || argName == L"-cd") {
      // We don't check for negative delays here, because the server handles it gracefully
      auto delayMs = _wtoi(*argPos++);
      args.config.DatabaseCloseDelay(std::chrono::milliseconds(delayMs));

    } else if (argName == L"--port" || argName == L"-p") {
      // Specify the port to listen on 
      auto port = _wtoi(*argPos++);
      if (port < 0 || port > std::numeric_limits<uint16_t>::max()) {
        std::wcerr << L"Invalid port number specified\n";
        return args;
      }
      args.config.Port(port);

    } else if (argName == L"--maxmemory" || argName == L"-mm") {
      if (auto memoryLimit = ParseMemoryLimit(*argPos++)) {
        args.config.ServerMemoryLimit(*memoryLimit);
      } else {
        std::wcerr << L"Invalid server memory limit specified\n";
        return args;
      }
    
    } else if (argName == L"--help" || argName == L"-h") {
      // --help specified
      args.help = true;
    } else {
      std::wcerr << "Unsupported command line argument: " << argName << "\n" << "Run with --help to display suported arguments\n";
      return args; // invalid args
    }
  }

  args.valid = true;
  return args;
}


void Args::PrintHelp() const {
  std::cout
    << "Usage: blobsdb_server.exe [arguments]\n"
    << " Arguments:\n"
    << "  --test                     Run small server internal unittests. Combine with --help to see options\n"
    << "  --help,-h                  Displays this help message\n"
    << "  --logfile,-lf  <filename>  Server will write log statements into the specified file instead of the console\n"
    << "  --loglevel,-ll <level>     Set the logging level as one of OFF,DEBUG,INFO,WARN,ERROR\n"
    << "  --dbroot,-dr <path>        Set the database root directory. All database file paths will be relative to that\n"
    << "                             directory and only database files from that directory can be opened.\n"
    << "                             Default: .\\databases\n"
    << "  --nodbroot,-ndr            Disables the database root and allows opening a database file in any directory.\n"
    << "                             When passing relative database paths, they will be resolved relative to the\n"
    << "                             server's working directory.\n"
    << "  --closedelay,-cd <delay>   Sets the delay (in ms) to close unused databases after and release used memory.\n"
    << "                             Default: 0 = no delay\n"
    << "  --port,-p <port>           Sets the port to listen on.\n"
    << "                             Default: 8108\n"
    << "  --maxmemory,-mm <limit>    Sets the maximum memory the server should allocate for keeping blobs, clusters,\n"
    << "                             segments loaded in memory. Beyond this point memory will be unloaded and has to\n"
    << "                             be re-read from the database file.\n"
    << "                             The size may be specified as number of bytes followed by an optional suffix\n"
    << "                             (k, M, G, T, P, E) eg. '2G'\n"
  ;
}



const char** Args::ToAsciiArgv(int argc, const wchar_t* const* argv) {
  static std::vector<std::string> asciiArgs;
  static std::vector<const char*> asciiArgv;

  // Called for the first time -> perform conversion
  if (asciiArgv.empty()) {
    for (int i = 0; i < argc; ++i) {
      std::string asciiArg;
      for (wchar_t ch : std::wstring_view(argv[i])) {
        if (ch > 0 && ch <= 0x7F) {
          asciiArg.push_back(static_cast<char>(ch));
        }
      }
      asciiArgs.push_back(asciiArg);
    }

    asciiArgv.resize(asciiArgs.size());
    std::transform(asciiArgs.begin(), asciiArgs.end(), asciiArgv.begin(), [](const std::string& asciiArg) { return asciiArg.c_str(); });
  }

  return asciiArgv.data();
}


std::optional<size_t> Args::ParseMemoryLimit(const wchar_t* arg) {
  std::wregex regex(L"^([0-9]+)([kMGTP])?$");
  std::wsmatch matchResult;
  std::wstring argStr = arg;

  if (std::regex_match(argStr, matchResult, regex)) {
    size_t memory = 0;
    std::wistringstream stream(matchResult[1].str());
    stream >> memory;
    
    if (matchResult[2].matched) {
      switch (*matchResult[2].first) {
        case L'k': memory *= 1_kB; break;
        case L'M': memory *= 1_MB; break;
        case L'G': memory *= 1_GB; break;
        case L'T': memory *= 1_TB; break;
        case L'P': memory *= 1_PB; break;
      }
    }

    return memory;
  }

  return std::nullopt;
}