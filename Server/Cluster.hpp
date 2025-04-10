#pragma once

#include "Blob.hpp"

namespace blobs {
namespace server {

class Cluster {
public:
  Cluster(uint32_t id);

  const uint32_t id;
private:
  uint32_t lastBlobId;

  std::unordered_map<uint32_t, std::unique_ptr<Blob>> blobs;
};



}}