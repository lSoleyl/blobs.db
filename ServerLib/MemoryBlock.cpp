#include "pch.hpp"
#include <server/MemoryBlock.hpp>
#include <server/LRUCache.hpp>


namespace blobs::server {

MemoryBlock::MemoryBlock() : status(Status::LOADED) {}


size_t MemoryBlock::EvictSize() const {
  assert(!"EvictSize() must be implemented for LRU cache elements!");
  return 0;
}

void MemoryBlock::Evict() {
  assert(!"Evict() must be implemented for LRU cache elements!");
}


void MemoryBlock::Released() {
  if (lru.inCache) {
    LRUCache::Remove(*this);
  }

  fileLocation.Clear();
}

void MemoryBlock::AllocatedInFile() {}


void MemoryBlock::OnDestroy() {
  if (lru.inCache) {
    // Remove from LRU cache and update used/evictable memory accordingly
    LRUCache::Remove(*this);
  }

  // Remove does not touch the used memory
  LRUCache::usedMemory -= EvictSize();
}


}
