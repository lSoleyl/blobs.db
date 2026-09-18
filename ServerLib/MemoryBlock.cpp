#include "pch.hpp"
#include <server/MemoryBlock.hpp>
#include <server/LRUCache.hpp>


namespace blobs::server {

MemoryBlock::MemoryBlock() : status(Status::LOADED) {}

FIXME(
  "We cannot Remove() the block from LRU cache in the MemoryBlock destructor, because Remove relies on the removed size being known, "
  "which it isn't once we enter the base class' destructor."
)



}
