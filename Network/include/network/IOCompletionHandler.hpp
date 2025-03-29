#pragma once

#include "..\win_include.hpp"

namespace blobs {
namespace network {



class IOCompletionHandler {
public:
  virtual void HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) = 0;
};

}}
