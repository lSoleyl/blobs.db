#include "Args.hpp"

#include <string>
#include <vector>
#include <algorithm>


Args::Args() {}

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
        args.logLevel = blobs::server::logging::Level::OFF_LEVEL;
      } else if (argValue == L"DEBUG") {
        args.logLevel = blobs::server::logging::Level::DEBUG_LEVEL;
      } else if (argValue == L"INFO") {
        args.logLevel = blobs::server::logging::Level::INFO_LEVEL;
      } else if (argValue == L"WARN") {
        args.logLevel = blobs::server::logging::Level::WARN_LEVEL;
      } else if (argValue == L"ERROR") {
        args.logLevel = blobs::server::logging::Level::ERROR_LEVEL;
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
      args.logFile = *argPos++;
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
    << "Usage: server.exe [arguments]\n"
    << " Arguments:\n"
    << "  --test                     Run small server internal unittests. Combine with --help to see options\n"
    << "  --help,-h                  Displays this help message\n"
    << "  --logfile,-lf  <filename>  Server will write log statements into the specified file instead of the console\n"
    << "  --loglevel,-ll <level>     Set the logging level as one of OFF,DEBUG,INFO,WARN,ERROR\n";
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