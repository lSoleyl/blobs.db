#include "pch.hpp"
#include <server/Blob.hpp>
#include <server/FileBackend.hpp>

namespace blobs {
namespace server {

Blob::Blob(blob_id id, commit_id commitId) : id(id), commitId(commitId) {}


void Blob::SetContent(std::string_view blobContent) {
  data.assign(reinterpret_cast<const uint8_t*>(blobContent.data()), reinterpret_cast<const uint8_t*>(blobContent.data())+blobContent.size());
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
  
  status = Status::LOADED;
}


}}