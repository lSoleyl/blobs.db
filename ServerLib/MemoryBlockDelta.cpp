#include "pch.hpp"
#include <server/MemoryBlockDelta.hpp>


namespace blobs::server {





void MemoryBlockDelta::Released(MemoryBlock* block) {
  if (allocated.find(block) != allocated.end()) {
    // Block has been first allocated, then released again -> remove from both sets as this block should not affect the free list's state
    allocated.erase(block);
  } else {
    // Mark as released - it shouldn't already be marked as released as this would indicate a double free error
    // somewhere in the update process
    assert(released.find(block) == released.end());
    
    released.insert(block);
  }
}

void MemoryBlockDelta::Allocated(MemoryBlock* block) {
  // Marking a block as allocated, which has previously been marked as released is a grave error and should never happen during an update!
  assert(released.find(block) == released.end());

  // We may consider this a logic error too if we assmue that Allocated() should only be called once the new block is actually allocated and that
  // should never happen twice during a single transaction update...
  assert(allocated.find(block) == allocated.end());

  allocated.insert(block);
}


const std::unordered_set<MemoryBlock*>& MemoryBlockDelta::GetReleased() const {
  return released;
}

const std::unordered_set<MemoryBlock*>& MemoryBlockDelta::GetAllocated() const {
  return allocated;
}





}