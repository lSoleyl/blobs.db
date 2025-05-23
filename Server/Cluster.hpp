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


  /** Returns the next free blob id for this cluster
   *  This is the value, which can be read from the `NextFreeBlobId` blob
   */
  blob_id GetNextFreeBlobId() const;


  const cluster_id id;
private:
  blob_id nextFreeBlobId;

  // We allow holes in blob numbering due to deletion, so we need a map to hold all blobs of a cluster
  std::unordered_map<blob_id, std::unique_ptr<Blob>> blobs;

  /** When requesting the `NextFreeBlobId` blob via GetBlob() we want to be able to return a blob to avoid too much special handling in
   *  Server::TryHandleBlobsRead(). This blob however is not stored in the blobs map as this is rather considered cluster metadata than an
   *  actual child blob. Not to mention the wasted database space if we were to store this single number in its own blob.
   */
  Blob nextFreeBlobIdBlob;
};



}}