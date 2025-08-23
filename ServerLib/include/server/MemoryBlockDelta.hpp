#pragma once


namespace blobs::server {

struct MemoryBlock;


/** This class is used to keep track of released and newly allocated memory blocks while applying a commit message to the database.
 *  The memory blocks are first only marked for allocation (in case their size changes due to multiple updates in the same commit message)
 *  and only after all commit messages have been applied, their blocks are actually allocated from the free list and the free list is updated accordingly.
 */
class MemoryBlockDelta {
public:

  /** Called to mark a block as released as it has been replaced by a new version in the copy-on-write update
   */
  void Released(MemoryBlock* block);

  /** Called to mark a memory block as allocated during the copy-on-write update
   */
  void Allocated(MemoryBlock* block);


  const std::unordered_set<MemoryBlock*>& GetReleased() const;
  const std::unordered_set<MemoryBlock*>& GetAllocated() const;

private:
  std::unordered_set<MemoryBlock*> released;
  std::unordered_set<MemoryBlock*> allocated;
};





}


