#pragma once

#include <optional>

#include <server/Logging.hpp>

/** Command line argument parser
 */
class Args {
public:
  /** Parses the command line arguments, extracts the arguments relevant to the server and returns the information as and Args object
   *  Throws an exception if illegal arguments are passed.
   */
  static Args Parse(int argc, const wchar_t* const* argv);

  /** Prints a help message to the console for the command line use of the server
   */
  void PrintHelp() const;

  /** Converts the wide char argument array into an ASCII only argument array as is needed by the doctest argument parser.
   *  Non ASCII characters are simply skipped during translation (no unicode support for doctest arguments).
   *  The returned array is statically allocated and does not need to be freed.
   */
  static const char** ToAsciiArgv(int argc, const wchar_t* const* argv);


  bool runTests = false; // --test passed?
  bool help = false;     // --help passed?

  /** True if specified args are valid
   */
  explicit operator bool() const { return valid; }

  blobs::server::logging::Level logLevel = blobs::server::logging::Level::INFO_LEVEL; // --loglevel passed
  std::optional<std::wstring> logFile;                                                // --logfile passed

  /**
   * default: ".\databases"
   * can be overwritten by passing --dbroot <path>
   * can be disabled to be able to open any database on the filesystem using --nodbroot
   */
  std::optional<std::wstring> dbRoot = L".\\databases";

private:
  bool valid = false;
  Args();
};