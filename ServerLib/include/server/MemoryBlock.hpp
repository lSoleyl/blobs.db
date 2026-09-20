#pragma once

#include "File.hpp"

namespace blobs::server {


/** A base class struct to use for all classes, whose data is allocated in memory blocks in the database file
 */
struct MemoryBlock {
  MemoryBlock();

  /** This method must be implemented by all structures, which can be allocated inside the file database.
   *  After modifying such a data structure, this method should calculate the new required memory size for storing it.
   */
  virtual uint64_t CalculateRequiredSize() const = 0;

  /** This method should serialize the memory block object into the provided buffer to write it into file.
   */
  virtual void SerializeIntoBuffer(std::vector<char>& targetBuffer) const = 0;

  /** Should be overwritten by blob,cluster,segment to return the memory that would be returned when evicting
   *  that element. This should only include memory released by resetting it to the NOT_LOADED state, not deleting it.
   */
  virtual size_t EvictSize() const;

  /** This method is called on the least recently used element of the LRU cache.
   *  The called method must only reset the element into the NOT_LOADED state and relase EvictSize() bytes.
   *  The caller (LRUCache) will handle removing the element from the cache and updating the memory size values.
   */
  virtual void Evict();


  /** Called by the server during commit on all memory blocks that are not part of the current snapshot anymore.
   *  This method will remove this memory block from the LRU cache (if it is in there) and reset the file location to an empty 
   *  location. This is important as we MUST NOT evict memory blocks that cannot be reloaded from disk.
   */
  void Released();

  /** Called by the server during commit on all memory blocks that have been newly allocated to a file backend.
   *  Blobs,Clusters,Segments should register themselves to the LRU cache as they can now be evicted.
   *  The default implementation of this method does nothing.
   */
  virtual void AllocatedInFile();

protected:

  /** Call this method in the destructor of any class deriving from the MemoryBlock that is managed by the LRU cache.
   *  This will correctly deregister this element from the LRU cache. Call this method before releasing any memory in the derived class.
   *  We sadly cannot call this automatically in ~MemoryBlock(), because it relies on EvictSize() being known, but that virtual function
   *  would not be callable anymore in ~MemoryBlock()
   */
  void OnDestroy();



public:
  /** Where in the file this object is allcoated
   */
  file::BlockReference fileLocation;


  // We use pragma pack here to avoid 7 padding bytes after inCache and to merge it with the status member following it.
  #pragma pack(push, 1)
  struct LRU {
    /** Pointer to previous element in doubly linked LRU cache list
     */
    MemoryBlock* prev = nullptr;

    /** Pointer to next element in doubly linked LRU cache list
     */
    MemoryBlock* next = nullptr;

    /** A separate boolean keeping track of whether the element is part of the LRU cache.
     *  Knowing this allows us to avoid some overhead and costs us no memory overhead
     */
    bool inCache = false;
  };
  #pragma pack(pop)

  // We specify alignas to not reduce the alignment of the whole class down to 1 byte
  alignas(MemoryBlock*) LRU lru;



  enum class Status : uint8_t {
    NOT_LOADED,
    //LOADING, // currently unused, because we load synchronously
    LOADED
  };

  Status status;
};






}