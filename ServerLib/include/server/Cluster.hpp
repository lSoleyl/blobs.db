#pragma once

#include "Blob.hpp"
#include "sorted_flat_map.hpp"

namespace blobs {
namespace server {

class MemoryBlockDelta;
class FileBackend;

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
   * 
   * @param blob the blob id to retrieve the blob for
   * @param delta the data structure used to keep track of created/deleted memory blocks during copy-on-write commit processing
   *              This may be nullptr for pure in memory databases
   */
  Blob* UpdateBlob(blob_id blob, MemoryBlockDelta* delta);


  /** Removes the specified blob from the blob map. This doesn't `delete` the blob itself.
   *  This will be done by the std::shared_ptr if this cluster was the last one referencing it.
   * 
   * @param blob the blob id of the blob to delete
   * @param delta the data structure used to keep track of created/deleted memory blocks during copy-on-write commit processing
   *              This may be nullptr for pure in memory databases
   */
  void DeleteBlob(blob_id blob, MemoryBlockDelta* delta);


  /** Called by Segment::DeleteCluster() to ensure the cluster marks all of its blobs
   *  as released too to not leak their memory blocks. The blob map itself is not modifed by this operation because this
   *  cluster may still be referenced by an MVCC snapshot.
   */
  void ReleaseAllBlobs(MemoryBlockDelta* delta);

  /** Returns the next free blob id for this cluster
   *  This is the value, which can be read from the `NextFreeBlobId` blob
   */
  blob_id GetNextFreeBlobId() const;


  /** Sets the nextFreeBlobId member and updates the contents of the corresponding blob
   */
  void SetNextFreeBlobId(blob_id nextFreeId);

  /** Calculate the size of the Cluster's memory block if stored in file
   */
  virtual uint64_t CalculateRequiredSize() const override;

  /** Serialize the cluster into a buffer for writing it to file
   */
  virtual void SerializeIntoBuffer(std::vector<char>& targetBuffer) const override;

  /** Called by Segment::GetBlob() if the cluster wasn't loaded yet to initialize the cluster's data and mark it as loaded
   *  This will load the map of blobs and initialize each blob to a not yet loaded one
   */
  void LoadFrom(const FileBackend& file);

  const cluster_id id;

  /** The commit id of the transaction when this cluster's blob table has been last modified
   *  We cannot mark it const anymore, because when loading a cluster from file on demand we must change the 
   *  commitId, which we do now know beforehand unless we would also store it in the block reference, which would be absurd.
   */
  commit_id commitId;
private:

  /** Used when loading a cluster to mark the blob as exisitng, but not yet loaded from file
   */
  void DelayLoadBlob(blob_id blob, const file::BlockReference& fileLocation);

  blob_id nextFreeBlobId;

  // We allow holes in blob numbering due to deletion, so we need a map to hold all blobs of a cluster
  // The blobs are held as shared_ptrs to perform safe copy on write updates
  // We use a sorted vector of pairs instead of an actual map as this should have the best performance metrics
  // for our use cases and is better suited for storage in the file database format.
  sorted_flat_map<blob_id, std::shared_ptr<Blob>> blobs;

  /** When requesting the `NextFreeBlobId` blob via GetBlob() we want to be able to return a blob to avoid too much special handling in
   *  Server::TryHandleBlobsRead(). This blob however is not stored in the blobs map as this is rather considered cluster metadata than an
   *  actual child blob. Not to mention the wasted database space if we were to store this single number in its own blob.
   */
  Blob nextFreeBlobIdBlob;
};



}}