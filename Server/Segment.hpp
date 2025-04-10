#pragma once

#include "Cluster.hpp"

namespace blobs {
namespace server {

class Segment {
public:
  Segment(uint32_t id);

  const uint32_t id;
private:
  uint32_t lastClusterId;
  std::unordered_map<uint32_t, std::unique_ptr<Cluster>> clusters;
};

}}

