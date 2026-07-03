#pragma once

namespace blobs {

/** Log level constants.
 *  All log levels have the _LEVEL suffix to avoid name collisions with macros like ERROR(defined in windows.h)
 */
enum class LogLevel : int {
  OFF_LEVEL,
  ERROR_LEVEL,
  WARN_LEVEL,
  INFO_LEVEL,
  DEBUG_LEVEL
};

}