#include "pch.hpp"
#include <blobs/MultiBlobRequest.hpp>
#include <blobs/Exception.hpp>
#include <network/message/BlobsRead.hpp>

namespace blobs {


MultiBlobRequest::MultiBlobRequest(size_t nBlobs) : entries(nBlobs) {}
MultiBlobRequest::~MultiBlobRequest() {}

MultiBlobRequest* MultiBlobRequest::CreateInternal(size_t nBlobs) {
  if (nBlobs > std::numeric_limits<decltype(network::message::BlobsRead::nBlobsRequested)>::max()) {
    throw Exception("Too many blobs per request!");
  }
  return new MultiBlobRequest(nBlobs);
}

void MultiBlobRequest::Release() {
  delete this;
}


auto MultiBlobRequest::begin() -> BlobEntry* {
  return entries.data();
}

auto MultiBlobRequest::begin() const -> const BlobEntry* {
  return entries.data();
}

auto MultiBlobRequest::end() -> BlobEntry* {
  return entries.data() + entries.size();
}

auto MultiBlobRequest::end() const -> const BlobEntry* {
  return entries.data() + entries.size();
}


auto MultiBlobRequest::operator[](size_t i) -> BlobEntry& {
  return entries[i];
}

auto MultiBlobRequest::operator[](size_t i) const -> const BlobEntry& {
  return entries[i];
}

size_t MultiBlobRequest::Size() const {
  return entries.size();
}


const void* MultiBlobRequest::CopyIntoCache(const void* data, size_t size) {
  std::vector<uint8_t> copy(size);
  std::memcpy(copy.data(), data, size);
  blobCache.push_back(std::move(copy));
  return blobCache.back().data();
}


void MultiBlobRequest::ClearCache() {
  blobCache.clear();
}


}