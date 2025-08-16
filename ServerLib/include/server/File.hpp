#pragma once

#include <cstdint>


/** Header containing the database file data structures
 */

namespace blobs::server::file 
{

/** Everything, which can be overwritten (in copy-on-write) will be allocated in memory blocks to be able to return them into the free list when they are no longer needed
 *  At the moment a memory block has no own data members, but we could add a block type and a block size in here if we want some way to validate the database file's consistency.
 */
struct MemoryBlock {};



/** We store the size of a block inside the reference to that block. This externalization of the block's size
 *  will ensure that we only need to perform one read access to read the whole block and not two reads (size & then content).
 *  This externalization however also makes recovery of a broken databse more difficult, but we could mitigate that by additionally storing
 *  some header information in each block (like a block type and size). This redundancy could make the database more resistant to data loss...
 *  But even the best header information is useless if it itself is damaged, so for now we only store the block size externally.
 */
struct BlockReference {
  BlockReference(); // default initializes both values to 0
  BlockReference(uint64_t offset, uint64_t size);

  uint64_t offset; // File offset to the block
  uint64_t size;   // Size of the referenced memory block

  uint64_t EndOffset() const; // returns the past end block offset (positioned right after the block in the file)
};




/** This struct represents a range of block references of a sepcific type (blob_id, cluster_id, segment_id)
 *  The range encoding is used to save some space as the allocated numbers are in most cases sequential.
 */
template<typename T>
struct Range {
  T startId; // first id 
  T endId;   // past end id

  /** Pointer to the file offset of the first element in the range
   */
  BlockReference* begin() {
    // The first entry starts right after the Range object itself in memory
    return reinterpret_cast<BlockReference*>(this + 1);
  }

  /** Past end pointer to the endId file offset(don't dereference obviously)
   */
  BlockReference* end() {
    auto nEntries = endId - startId;
    return begin() + nEntries;
  }
};

/** This class implements an iterator over a sequence of Range objects.
 */
template<typename T>
class RangesIterator {
public:
  using value_type = Range<T>;
  using pointer = value_type*;
  using reference = value_type&;

  RangesIterator(pointer pos) : pos(pos) {}

  bool operator==(const RangesIterator& other) const { return pos == other.pos; }
  bool operator!=(const RangesIterator& other) const { return pos != other.pos; }

  RangesIterator& operator++() {
    // The following code is correct as the past end position of the range is 
    // where the next range will start in memory as they are layouted one after another.
    pos = reinterpret_cast<Range<T>*>(pos->end());
    return *this;
  }

  reference operator*() const {
    return *pos;
  }

  pointer operator->() const {
    return pos;
  }

private:
  pointer pos;
};


/** Every database file starts with the Database structure
 */
struct Database {
  struct Header {
    char typeId[8]; // 'blobs.db' - used to distinguish database files from other files

    /** Configured datatype sizes are stored in the database header
     */
    struct Sizes {
      uint8_t segmentId;
      uint8_t clusterId;
      uint8_t blobId;
      uint8_t blobSize;
      uint8_t commitId;
    };

    Sizes sizes;

    /** Initializes the header to hold valid values according to the current conifguration
     */
    void Initialize();

    /** Returns true if all the header fields match the current configuration
     */
    bool IsValid() const;
  };

  /** The database root consists of three offsets to the main data structures, which are modified by each transaction
   */
  struct Root {
    BlockReference snapshot;        // File offset and size to the current databse snapshot block
    BlockReference freeList;        // File offset and size to the free list block
    BlockReference transactionLog;  // File offset and size to the transaction log block
  };

  Header header;
  uint8_t rootIndex;  // Index of the currently active database root in our copy on write update
  uint8_t unused[2];
  Root roots[2];      // Two root datastructures (one active, one inactive to support copy-on-write)
};






/** The free list used to store the a list of all free memory blocks with their corresponding sizes
 *  The number of entries is implicitly derived from the size of the memory block
 */
struct FreeList : public MemoryBlock {
  /** End file offset(end of the memory region managed by this FreeList - everthing following that offset can be considered garbage)
   *  We define this field here for the specific use case where the file has already been grown to allocate a new block, but the server crashes
   *  before the new FreeList is written into the Database::Root. If we load the grown database with this FreeList as still being active, we know that
   *  the memory following the endOffset is garbage and can be reused for new blocks, which we otherwise would have to assume being in use.
   */
  uint64_t endOffset;


  /** Each entry of the free list represents one unused memory block.
   *  Using 16 bytes per free block seems like a lot of waste and we could probably do better by either using relative offsets and limit
   *  blocks to 4GB, but that could turn into a major invonvenience down the road.
   *  Using var int encoding could also save space, but would make the encoding step computationally more expensive.
   */
  BlockReference* begin();
  /** Since the MemoryBlock at the moment doesn't know its size, it must be passed in as parameter
   */
  BlockReference* end(uint64_t blockSize);
};




/** The snapshot holds the commit id and the segment list as well as the next free segment id
 */
struct Snapshot : public MemoryBlock {
  commit_id commitId;
  segment_id nextFreeSegmentId;

  using SegmentRange = Range<segment_id>;
  using iterator = RangesIterator<segment_id>;

  iterator begin();
  
  /** Since the MemoryBlock at the moment doesn't know its size, it must be passed in as parameter
   */
  iterator end(uint64_t blockSize);
};

TODO("Should I use pragma pack to avoid the padding in between?");

/** The segment holds the commit id and the clusters as well as the segment id which is only used
 *  as a consistency check since the segment id is already known from the snapshot's segment range.
 */
struct Segment : public MemoryBlock {
  commit_id commitId;
  cluster_id nextFreeClusterId;
  segment_id id;

  using ClusterRange = Range<cluster_id>;
  using iterator = RangesIterator<cluster_id>;

  iterator begin();

  /** Since the MemoryBlock at the moment doesn't know its size, it must be passed in as parameter
   */
  iterator end(uint64_t blockSize);
};


/** The cluster holds the commit id, the blobs as well as the cluster id, which is only used as
 *  a consistency check since the cluster id is already known from the segment's custer range.
 */
struct Cluster : public MemoryBlock {
  commit_id commitId;
  blob_id nextFreeBlobId;
  cluster_id id;

  using BlobRange = Range<blob_id>;
  using iterator = RangesIterator<blob_id>;

  iterator begin();

  /** Since the MemoryBlock at the moment doesn't know its size, it must be passed in as parameter
   */
  iterator end(uint64_t blockSize);
};


struct Blob : public MemoryBlock {
  commit_id commitId;
  
  /** Since the MemoryBlock at the moment doesn't know its size, it must be passed in as parameter
   */
  std::string_view Data(uint64_t blockSize);
};





TODO("Implement transaction log later")
struct TransactionLog : public MemoryBlock {
  TODO("Define structure for transaction log entries");
};


}