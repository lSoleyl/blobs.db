#pragma once

namespace blobs {
namespace server {

class Blob {
public:
  // Initialize an empty blob
  Blob(blob_id id);

  const blob_id id;
private:
  commit_id commitId; // id of commit/transaction when this blob was created/written
  std::vector<uint8_t> data;
};

}}