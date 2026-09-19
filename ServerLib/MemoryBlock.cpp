#include "pch.hpp"
#include <server/MemoryBlock.hpp>
#include <server/LRUCache.hpp>


namespace blobs::server {

MemoryBlock::MemoryBlock() : status(Status::LOADED) {}



void MemoryBlock::OnDestroy() {
  if (lru.inCache) {
    // Remove from LRU cache and update used/evictable memory accordingly
    LRUCache::Remove(*this);
  }

  // Remove does not touch the used memory
  LRUCache::usedMemory -= EvictSize();
}


FIXME(
  "We cannot Remove() the block from LRU cache in the MemoryBlock destructor, because Remove relies on the removed size being known, "
  "which it isn't once we enter the base class' destructor."
)



}
