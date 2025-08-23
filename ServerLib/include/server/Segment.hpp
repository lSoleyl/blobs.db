#pragma once

#include "Cluster.hpp"

namespace blobs {
namespace server {

struct MemoryBlockDelta;

class Segment : public MemoryBlock {
public:
  Segment(segment_id id, commit_id commitId);

  /** Copy constructor used during transaction commit whenever the segment's cluster table changes
   */
  Segment(const Segment& other, commit_id commitId);

  /** Returns the cluster with the specified id or nullptr if it doesn't exist.
   *  This method CANNOT handle the nextFreeClusterId id, use GetBlob() instead
   */
  Cluster* GetCluster(cluster_id cluster);

  /** This method is used by the snapshot's ApplyCommitMessage method to fetch the cluster with the specified id
   *  and copy it if it hasn't been modified in the same transaction as the segment yet or create the cluster if id doesn't exist yet.
   *
   * @param cluster the cluster id to retrieve the cluster for
   * @param delta the data structure used to keep track of created/deleted memory blocks during copy-on-write commit processing
   *              This may be nullptr for pure in memory databases
   */
  Cluster* UpdateCluster(cluster_id cluster, MemoryBlockDelta* delta);

  /** Deletes the specified cluster from this segment by removing it from the cluster map. If this was the last reference to that cluster
   *  then it will be 'delete'd
   * 
   * @param cluster the cluster id of the cluster to delete
   * @param delta the data structure used to keep track of created/deleted memory blocks during copy-on-write commit processing
   *              This may be nullptr for pure in memory databases
   */
  void DeleteCluster(cluster_id cluster, MemoryBlockDelta* delta);

  /** Called by Snapshot::DeleteSegment() to ensure the segment marks all of its clusters as released
   *  too to not leak their memory blocks. The cluster map itself is not modified by this operation because this
   *  segment may still be referenced by an MVCC snapshot.
   */
  void ReleaseAllClusters(MemoryBlockDelta* delta);

  /** Returns the specified cluster's blob or nullptr if it doesn't exist
   */
  Blob* GetBlob(cluster_id cluster, blob_id blob);

  /** Returns the next free cluster id for this segment
   *  This is the value, which can be read from the (`NextFreeClusterId`, `NextFreeBlobId`) blob
   */
  cluster_id GetNextFreeClusterId() const;

  /** Updates the nextFreeClusterId and the contents of the blob holding the id
   */
  void SetNextFreeClusterId(cluster_id nextFreeId);

  /** Calculate the size of the Segment's memory block if stored in file
   */
  virtual uint64_t CalculateRequiredSize() const override;


  const segment_id id;

  /** The commit id of the transaction when the cluster map has been modified last time
   */
  const commit_id commitId;
private:
  cluster_id nextFreeClusterId;
  sorted_flat_map<cluster_id, std::shared_ptr<Cluster>> clusters;

  /** When requesting the `NextFreeClusterId` cluster via GetBlob() we want to be able to return a blob to avoid too much special handling in
   *  Server::TryHandleBlobsRead(). This blob however is not stored in the blobs map as this is rather considered segmetn metadata than an
   *  actual child blob. Not to mention the wasted database space if we were to store this single number in its own blob.
   */
  Blob nextFreeClusterIdBlob;
};

}}

