#include "pch.hpp"
#include "Blob.hpp"

namespace blobs {
namespace server {

Blob::Blob(blob_id id, commit_id commitId) : id(id), commitId(commitId) {}

}}