#pragma once

#include "Blob.hpp"

namespace blobs {
namespace server {

class Cluster {
public:
  Cluster(cluster_id id);

  /** Returns the blob or nullptr if it doesn't exist
   */
  Blob* GetBlob(blob_id blob);

  const cluster_id id;
private:
  blob_id lastBlobId;

  // We allow holes in blob numbering due to deletion, so we need a map to hold all blobs of a cluster
  std::unordered_map<blob_id, std::unique_ptr<Blob>> blobs;
};



}}