#include "pch.hpp"
#include <server/Blob.hpp>
#include <server/FileBackend.hpp>
#include <server/LRUCache.hpp>


namespace blobs {
namespace server {

Blob::Blob(blob_id id, commit_id commitId) : id(id), commitId(commitId) {}


Blob::~Blob() {
  if (lru.inCache) {
    // Remove from LRU cache and update used/evictable memory accordingly
    LRUCache::Remove(*this);
  } 

  // Remove does not touch the used memory
  LRUCache::usedMemory -= EvictSize();
}

void Blob::SetContent(std::string_view blobContent) {
  auto memoryDelta = 0 - EvictSize(); // unsigned underflow is well defined
  data.assign(reinterpret_cast<const uint8_t*>(blobContent.data()), reinterpret_cast<const uint8_t*>(blobContent.data())+blobContent.size());
  memoryDelta += EvictSize(); // unsigned overflow is just as well defined
  // assumption: vector will never shrink its capacity because of an assign operation

  LRUCache::usedMemory += memoryDelta;
  if (lru.inCache) {
    LRUCache::evictableMemory += memoryDelta;
  }
}


std::string_view Blob::ReadContent() const {
  return std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
}



uint64_t Blob::CalculateRequiredSize() const {
  return sizeof(file::Blob) + data.size();
}

void Blob::SerializeIntoBuffer(std::vector<char>& targetBuffer) const {
  // We assume the memory block has been correctly allocated before this operation
  assert(CalculateRequiredSize() == fileLocation.size);
  targetBuffer.resize(fileLocation.size);

  auto fileBlob = reinterpret_cast<file::Blob*>(targetBuffer.data());
  fileBlob->commitId = commitId;
  std::copy(data.begin(), data.end(), fileBlob->DataBegin());
}


void Blob::LoadFrom(const FileBackend& file) {
  assert(status != Status::LOADED); // We currently don't use the LOADING status, this will only be used once we migrate to async IO loading
  assert(file); // cannot be called on an in memory database (or one that failed to open)

  auto fileBlob = file.LoadMemoryBlock<file::Blob>(fileLocation);
  if (!fileBlob) {
    TODO("Better error handling?");
    throw std::exception("Failed to load blob!");
  }

  commitId = fileBlob->commitId;
  SetContent(fileBlob->Data(fileLocation.size));
  LRUCache::Enter(*this); // Enter into the LRU cache now that we loaded the blob from disk into memory

  status = Status::LOADED;
}

size_t Blob::EvictSize() const {
  return data.capacity(); // <- not size, because vector may have allocated more than just the content
}

void Blob::Evict() {
  assert(lru.evictable && lru.inCache);
  
  // move assign a new vector, because clear() does not change the capacity
  data = std::vector<uint8_t>{};
  status = Status::NOT_LOADED;
}


}}