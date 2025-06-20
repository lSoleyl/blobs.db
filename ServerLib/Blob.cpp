#include "pch.hpp"
#include "include/server/Blob.hpp"

namespace blobs {
namespace server {

Blob::Blob(blob_id id, commit_id commitId) : id(id), commitId(commitId) {}


void Blob::SetContent(std::string_view blobContent) {
  data.assign(reinterpret_cast<const uint8_t*>(blobContent.data()), reinterpret_cast<const uint8_t*>(blobContent.data())+blobContent.size());
}


std::string_view Blob::ReadContent() const {
  return std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
}

}}