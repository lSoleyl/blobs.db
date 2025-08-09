#pragma once

#include <cstdint>


/** Header containing the database file data structures
 */

namespace blobs::server::file 
{






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
    uint64_t snapshotOffset;        // File offset to the current databse snapshot
    uint64_t freelistOffset;        // File offset to the free file block list
    uint64_t transactionLogOffset;  // File offset to the transaction log
  };

  Header header;
  uint8_t rootIndex;  // Index of the currently active database root in our copy on write update
  uint8_t unused[2];
  Root roots[2];      // Two root datastructures (one active, one inactive to support copy-on-write)
};


/** Everything, which can be overwritten (in copy-on-write) will be allocated in memory blocks to be able to return them into the free list when they are no longer needed
 */
struct MemoryBlock {
  uint64_t size; // Size of this memory block including this size field - we allow memory block sizes larger than 4 GB to be able to store huge blobs / datastructures if needed
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
  TODO("Use varint? - But then we wouldn't be able to preallocate the size of the freeList - Loading the freelist is only done once to be fair");
  struct FreeBlock {
    uint64_t blockOffset; // File offset to the free block
    uint64_t blockSize;   // Size of the block
  };

  FreeBlock* begin();
  FreeBlock* end();
};


template<typename T>
struct Range {
  T startId; // first id 
  T endId;   // past end id

  /** Pointer to the file offset of the first element in the range
   */
  uint64_t* begin() {
    // The first entry starts right after the Range object itself in memory
    return reinterpret_cast<uint64_t*>(this + 1);
  }

  /** Past end pointer to the endId file offset(don't dereference obviously)
   */
  uint64_t* end() {
    auto nEntries = endId - startId;
    return begin() + nEntries;
  }
};


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




/** The snapshot holds the commit id and the segment list as well as the next free segment id
 */
struct Snapshot : public MemoryBlock {
  commit_id commitId;
  segment_id nextFreeSegmentId;

  using SegmentRange = Range<segment_id>;
  using iterator = RangesIterator<segment_id>;

  iterator begin();
  iterator end();
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
  iterator end();
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
  iterator end();
};


struct Blob : public MemoryBlock {
  commit_id commitId;
  
  std::string_view Data();
};



TODO("Implement transaction log later")
struct TransactionLog : public MemoryBlock {
  uint64_t* begin(); // file offset of the first transaction log entry
  uint64_t* end();   // file offset of the past end transaction log entry


  TODO("Define structure for transaction log entries");
};


}