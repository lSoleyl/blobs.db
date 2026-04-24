#pragma once

#include <common/BlobLocation.hpp>
#include "Database.hpp"

namespace blobs::server {

struct BlobLocationRange {
  BlobLocationRange();

  /** A range encompassing the whole segment
   */
  BlobLocationRange(segment_id segment);

  /** A range encompassing the whole cluster
   */
  BlobLocationRange(segment_id segment, cluster_id cluster);

  /** A range encompassing the specified blob
   */
  BlobLocationRange(segment_id segment, cluster_id cluster, blob_id blob);

  /** A range encompassing the specified blob range inside a cluster and segment
   */
  BlobLocationRange(segment_id segment, cluster_id cluster, blob_id blobBegin, blob_id blobEnd);

  /** Returns true if this range represents the creation of a cluster (strongly simplified check here relies on EnterNewCluster())
   */
  bool IsCreatedCluster() const;

  /** Returns true if this range represents the creation of a segment (strongly simplified check here relies on EnterNewSegment())
   */
  bool IsCreatedSegment() const;

  BlobLocation begin, end; // regular [begin;end) interval with exclusive end
};


class BlobLocationRanges {
public:
  /** True if any entry encompasses the given location
   */
  bool Encompasses(const BlobLocation& location) const;

  /** True if any entry encompasses the whole passed range
   */
  bool Encompasses(const BlobLocationRange& checkRange) const;

  /** True if an entry corresponding to EnterNewCluster() exists for the specified cluster
   */
  bool EncompassesCreatedCluster(segment_id segment, cluster_id cluster) const;

  /** True if an entry corresponding to EnterNewSegment() exists for the specified segment
   */
  bool EncompassesCreatedSegment(segment_id segment) const;


  void Enter(const BlobLocationRange& range);


  /** Constructs and enters the ranges for all implicitly created blobs when creating a new cluster
   */
  void EnterNewCluster(segment_id segmentId, cluster_id clusterId);

  /** Constructs and enters the ranges for all implicitly created blobs and the cluster
   */
  void EnterNewSegment(segment_id segmentId);

  using iterator = std::vector<BlobLocationRange>::iterator;
  iterator begin();
  iterator end();

  using const_iterator = std::vector<BlobLocationRange>::const_iterator;
  const_iterator begin() const;
  const_iterator end() const;

private:
  std::vector<BlobLocationRange> ranges;
};

/** This data structure is used during commit message validation to keep track of all created blob ranges and validate that a client is 
 *  allowed to create a certain blob. Implementing this structure was necessary to support CreateBlobAt()
 */
class BlobCreationRanges : public BlobLocationRanges {
public:
  /** This method is used to track all committed NextFreeBlobId blobs to be able to validate whether a 
   *  blob creation is allowed or not.
   */
  void NextFreeBlobIdCommitted(segment_id segment, cluster_id cluster, blob_id nextFreeBlobId);


  /** This method will validate that a NextFreeBlobId has been committed for the corresponding cluster and
   *  that the blob id doesn't exceed the committed NextFreeBlobId and if so will also enter this location as created blob
   *  to later transfer a sticky write lock to the client.
   */
  bool TryCreateBlob(const BlobLocation& location);


  /** Returns true if the location is encompassed by any created blob range.
   *  If not then it will check whether the blob does not exist in the database and can be created according to the committed NextFreeBlobId.
   *  If so then the location will be marked in the created ranges to transfer a sticky lock to the client.
   */
  bool EncompassesWithBlobCreation(const BlobLocation& location, Database& database);

private:
  /** All committed NextFreeBlobIds this is used to determine whether a specific blob may be created by the client.
   */
  std::vector<BlobLocation> committedNextFreeBlobIds;
};




}