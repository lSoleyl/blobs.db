#pragma once


#include <cassert>
#include <cstdint>
#include <memory>
#include <algorithm>

#include <unordered_map>
#include <map>
#include <set>
#include <deque>
#include <vector>
#include <string>
#include <optional>

// Not pretty but since we must distribute the client libs headers, they must be self contained
// and should be our single source of truth
#include "../ClientLib/include/blobs/Config.hpp"


#ifdef NDEBUG
// Remove all test code from release builds
#define DOCTEST_CONFIG_DISABLE
#endif

// Include latest doctest version as our unit testing framework (https://github.com/doctest/doctest/blob/master/doc/markdown/readme.md)
// If we need to use TEST_CASE_CLASS/TEST_SCENARIO_CLASS, we can still sync back to 06af20b9bbbceea87e2769bfcd9077051d1ce167
#include "doctest/doctest.h"
