#pragma once

#include "Blob.hpp"

namespace blobs {
namespace server {

class Cluster : public MemoryBlock {
public:
  Cluster(cluster_id id, commit_id commitId = 1);

  /** Copy constructor used to copy the cluster for modification in a new transaction snapshot.
   *
   * @param other the cluster to copy
   * @param commitId the snapshot's commit id
   */
  Cluster(const Cluster& other, commit_id commitId);

  /** Returns the blob or nullptr if it doesn't exist
   */
  Blob* GetBlob(blob_id blob);


  /** If the specified blob does not yet exist OR has not yet been modified in the same transaction as the clsuter 
   *  then a new EMPTY(!!!) blob is returned, which is entered into the blob map. Otherwise the already modified blob  
   *  is returned.
   * 
   *  We don't COPY the blob when modifying it during this transaction, because the only data to copy is the blob content 
   *  and we are only interested in modifying that data, so why copy something that will be overwritten anyway?
   */
  Blob* UpdateBlob(blob_id blob);


  /** Removes the specified blob from the blob map. This doesn't `delete` the blob itself.
   *  This will be done by the std::shared_ptr if this cluster was the last one referencing it.
   */
  void DeleteBlob(blob_id blob);

  /** Returns the next free blob id for this cluster
   *  This is the value, which can be read from the `NextFreeBlobId` blob
   */
  blob_id GetNextFreeBlobId() const;


  /** Sets the nextFreeBlobId member and updates the contents of the corresponding blob
   */
  void SetNextFreeBlobId(blob_id nextFreeId);


  const cluster_id id;

  /** The commit id of the transaction when this cluster's blob table has been last modified
   */
  const commit_id commitId;
private:
  blob_id nextFreeBlobId;

  // We allow holes in blob numbering due to deletion, so we need a map to hold all blobs of a cluster
  // The blobs are held as shared_ptrs to perform safe copy on write updates
  std::unordered_map<blob_id, std::shared_ptr<Blob>> blobs;

  /** When requesting the `NextFreeBlobId` blob via GetBlob() we want to be able to return a blob to avoid too much special handling in
   *  Server::TryHandleBlobsRead(). This blob however is not stored in the blobs map as this is rather considered cluster metadata than an
   *  actual child blob. Not to mention the wasted database space if we were to store this single number in its own blob.
   */
  Blob nextFreeBlobIdBlob;
};



}}