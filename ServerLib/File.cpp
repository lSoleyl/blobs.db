#include "pch.hpp"
#include <server/File.hpp>


namespace blobs::server::file {

BlockReference::BlockReference() : offset(0), size(0) {}

BlockReference::BlockReference(uint64_t offset, uint64_t size) : offset(offset), size(size) {}

uint64_t BlockReference::EndOffset() const {
  return offset + size;
}


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



BlockReference* FreeList::begin() {
  return reinterpret_cast<BlockReference*>(this+1);
}

BlockReference* FreeList::end() {
  return begin() + entryCount;
}

Snapshot::iterator Snapshot::begin() {
  return iterator(reinterpret_cast<SegmentRange*>(this+1));
}

Snapshot::iterator Snapshot::end(uint64_t blockSize) {
  return iterator(reinterpret_cast<SegmentRange*>(reinterpret_cast<uint8_t*>(this) + blockSize));
}


Segment::iterator Segment::begin() {
  return iterator(reinterpret_cast<ClusterRange*>(this+1));
}

Segment::iterator Segment::end(uint64_t blockSize) {
  return iterator(reinterpret_cast<ClusterRange*>(reinterpret_cast<uint8_t*>(this) + blockSize));
}


Cluster::iterator Cluster::begin() {
  return iterator(reinterpret_cast<BlobRange*>(this + 1));
}

Cluster::iterator Cluster::end(uint64_t blockSize) {
  return iterator(reinterpret_cast<BlobRange*>(reinterpret_cast<uint8_t*>(this) + blockSize));
}



std::string_view Blob::Data(uint64_t blockSize) const {
  const char* dataBegin = reinterpret_cast<const char*>(this + 1);
  return std::string_view(dataBegin, blockSize - sizeof(Blob));
}

char* Blob::DataBegin() {
  return reinterpret_cast<char*>(this + 1);
}


}
