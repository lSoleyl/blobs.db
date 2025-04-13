#pragma once

#include "Config.hpp"

#include <string_view>

namespace blobs {

class Exception {
public:
  Exception(std::string_view reason);
  BLOBS_EXPORT ~Exception();
  BLOBS_EXPORT const char* what() const;

private:
  std::string reason;
};


}