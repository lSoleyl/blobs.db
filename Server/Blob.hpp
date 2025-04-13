#pragma once

namespace blobs {
namespace server {

class Blob {
public:
  // Initialize an empty blob
  Blob(uint32_t id);

  const uint32_t id;
private:
  uint64_t commitId; // id of commit/transaction when this blob was created/written
  std::vector<uint8_t> data;
};

}}