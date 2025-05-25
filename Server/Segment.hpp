#pragma once

#include "Cluster.hpp"

namespace blobs {
namespace server {

class Segment {
public:
  Segment(segment_id id, commit_id commitId);

  /** Copy constructor used during transaction commit whenever the segment's cluster table changes
   */
  Segment(const Segment& other, commit_id commitId);

  /** Returns the cluster with the specified id or nullptr if it doesn't exist.
   *  This method CANNOT handle the nextFreeClusterId id, use GetBlob() instead
   */
  Cluster* GetCluster(cluster_id cluster);

  /** Returns the specified cluster's blob or nullptr if it doesn't exist
   */
  Blob* GetBlob(cluster_id cluster, blob_id blob);

  const segment_id id;
private:
  /** The commit id of the transaction when the cluster map has been modified last time
   */
  commit_id commitId;
  cluster_id nextFreeClusterId;
  std::unordered_map<cluster_id, std::shared_ptr<Cluster>> clusters;

  /** When requesting the `NextFreeClusterId` cluster via GetBlob() we want to be able to return a blob to avoid too much special handling in
   *  Server::TryHandleBlobsRead(). This blob however is not stored in the blobs map as this is rather considered segmetn metadata than an
   *  actual child blob. Not to mention the wasted database space if we were to store this single number in its own blob.
   */
  Blob nextFreeClusterIdBlob;
};

}}

