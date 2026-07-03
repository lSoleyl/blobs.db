#pragma once

#include <optional>

#include "../ClientLib/include/blobs/Initialization.hpp"

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


  /** The configuration object to parse the server options into
   */
  blobs::Configuration& config;

private:
  bool valid = false;
  Args();
};