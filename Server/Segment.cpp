#include "pch.hpp"
#include "Segment.hpp"


namespace blobs {
namespace server {

Segment::Segment(uint32_t id) : id(id), lastClusterId(0) {
  clusters.emplace(0, std::make_unique<Cluster>(0));
}




}}