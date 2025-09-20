// blobs.db pattern file
using commit_id = u64;
using segment_id = u32;
using cluster_id = u32;
using blob_id = u32;



struct DummyReference {
  u64 offset; // File offset to the block
  u64 size;   // Size of the referenced memory block
};


struct Blob<auto Offset, auto Size> {
  commit_id commitId;
  u8 data[Size-sizeof(commitId)];
};

struct BlobReference {
  u64 offset;
  u64 size;
  
  Blob<offset, size> blob @ offset;
};

struct BlobRange {
  blob_id startId;
  blob_id endId;
  
  BlobReference blobs[endId - startId];
};


struct Cluster<auto Offset, auto Size> {
  commit_id commitId;
  blob_id nextFreeBlobId;
  cluster_id id;
  
  BlobRange blobRanges[while ($ < Offset + Size)];
};


struct ClusterReference {
  u64 offset;
  u64 size;
  
  Cluster<offset, size> cluster @ offset;
};


struct ClusterRange {
  cluster_id startId; // At the moment cluster_id is u32
  cluster_id endId;
  
  ClusterReference clusters[endId - startId];
};


struct Segment<auto Offset, auto Size> {
  commit_id commitId;
  cluster_id nextFreeClusterId;
  segment_id id;
  ClusterRange clusterRanges[while ($ < Offset + Size)];   
};



struct SegmentReference {
  u64 offset;
  u64 size;  
  
  Segment<offset, size> segment @ offset;  
};


struct SegmentRange {
  segment_id startId; // At the moment segment_id is u32
  segment_id endId;
   
  SegmentReference segments[endId - startId];
};





struct Snapshot<auto Offset, auto Size> {
  commit_id commitId;
  segment_id nextFreeSegmentId;
  padding[4];
  SegmentRange segmentRanges[while ($ < Offset + Size)];   
};

struct SnapshotReference {
  u64 offset;
  u64 size;
  
  if (offset != 0) 
    Snapshot<offset, size> snapshot @ offset;
};

struct FreeBlockReference {
  u64 offset;
  u64 size;
  
  u8 freeMemory[size] @ offset [[color("1a1a1a")]];
};

struct FreeList<auto Offset, auto Size> {
  u64 endOffset; // End of managed file memory region
  u64 entryCount; // Number of entries in free list
  
  FreeBlockReference freeBlocks[entryCount];
};

struct FreeListReference {
  u64 offset;
  u64 size;
  
  if (offset != 0)
    FreeList<offset, size> freeList @ offset;
};




struct TransactionLog {

};


/** Configured datatype sizes are stored in the database header
 */
namespace Database {
    namespace Header {
        struct Sizes {
          u8 segmentId;
          u8 clusterId;
          u8 blobId;
          u8 blobSize;
          u8 commitId;
        };
    }
    
    struct Header {
        char typeId[8];
        Database::Header::Sizes sizes;
    };
    
    /** The database root consists of three offsets to the main data structures, which are modified by each transaction
     */
    struct Root {
      SnapshotReference snapshot;        // File offset and size to the current databse snapshot block
      FreeListReference freeList;        // File offset and size to the free list block
      DummyReference transactionLog;     // File offset and size to the transaction log block
    };
    
    struct DummyRoot {
      DummyReference snapshot;
      DummyReference freeList;
      DummyReference transactionLog;
    };
}



struct Database {
  Database::Header header;
  u8 rootIndex;     // Index of the currently active database root in our copy on write update
  u8 unused[2];
  
  // Two root datastructures (one active, one inactive to support copy-on-write)
  // Comment in the following line instead of the if-else to inspect the previous transaction's state
  //Database::Root roots[2];    
  
  // Only one database root is valid at a time
  if (rootIndex == 0) {
    Database::Root activeRoot;
    Database::DummyRoot inactiveRoot;
  } else {
    Database::DummyRoot inactiveRoot;  
    Database::Root activeRoot;
  }
  
};


Database db @ 0x0;