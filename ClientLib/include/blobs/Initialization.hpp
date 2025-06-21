#pragma once

#include "Config.hpp"


namespace blobs {

/** Must be called before any other blobs function
 */
BLOBS_EXPORT void Initialize();


/** This function should be called after all connections have been closed to perform some final cleanup operations
 */
BLOBS_EXPORT void Shutdown();

}