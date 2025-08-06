#include "pch.hpp"
#include "include/server/File.hpp"


namespace blobs::server::file {



void Database::Header::Initialize() {
  std::memcpy(&typeId, "blobs.db", sizeof(typeId));

  sizes.segmentId = sizeof(segment_id);
  sizes.clusterId = sizeof(cluster_id);
  sizes.blobId = sizeof(blob_id);
  sizes.blobSize = sizeof(blob_size);
  sizes.commitId = sizeof(commit_id);
}


bool Database::Header::IsValid() const {
  return std::string_view(typeId, sizeof(typeId)) == "blobs.db"
    && sizes.segmentId == sizeof(segment_id)
    && sizes.clusterId == sizeof(cluster_id)
    && sizes.blobId == sizeof(blob_id) 
    && sizes.blobSize == sizeof(blob_size)
    && sizes.commitId == sizeof(commit_id)
  ;
}



FreeList::FreeBlock* FreeList::begin() {
  return reinterpret_cast<FreeBlock*>(reinterpret_cast<uint8_t*>(this) + sizeof(FreeList));
}

FreeList::FreeBlock* FreeList::end() {
  return reinterpret_cast<FreeBlock*>(reinterpret_cast<uint8_t*>(this) + size);
}

Snapshot::iterator Snapshot::begin() {
  return iterator(reinterpret_cast<SegmentRange*>(this+1));
}

Snapshot::iterator Snapshot::end() {
  return iterator(reinterpret_cast<SegmentRange*>(reinterpret_cast<uint8_t*>(this) + size));
}


Segment::iterator Segment::begin() {
  return iterator(reinterpret_cast<ClusterRange*>(this+1));
}

Segment::iterator Segment::end() {
  return iterator(reinterpret_cast<ClusterRange*>(reinterpret_cast<uint8_t*>(this) + size));
}


Cluster::iterator Cluster::begin() {
  return iterator(reinterpret_cast<BlobRange*>(this + 1));
}

Cluster::iterator Cluster::end() {
  return iterator(reinterpret_cast<BlobRange*>(reinterpret_cast<uint8_t*>(this) + size));
}



std::string_view Blob::Data() {
  char* dataBegin = reinterpret_cast<char*>(this + 1);
  return std::string_view(dataBegin, size - sizeof(Blob));
}



}
