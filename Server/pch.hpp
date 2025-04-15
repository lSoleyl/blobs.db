#pragma once


#include <cassert>
#include <cstdint>
#include <memory>
#include <algorithm>

#include <unordered_map>
#include <map>
#include <vector>
#include <string>

// Not pretty but since we must distribute the client libs headers, they must be self contained
// and should be our single source of truth
#include "../ClientLib/include/blobs/Config.hpp"
