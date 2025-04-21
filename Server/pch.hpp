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
