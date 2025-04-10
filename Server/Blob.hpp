#pragma once

namespace blobs {
namespace server {

class Blob {
public:
  // Initialize an empty blob
  Blob(uint32_t id);

  const uint32_t id;
private:
  std::vector<uint8_t> data;
};

}}