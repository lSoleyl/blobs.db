#include "pch.hpp"
#include <server/BlobLocationRanges.hpp>

namespace blobs::server {


BlobLocationRange::BlobLocationRange() {}

BlobLocationRange::BlobLocationRange(segment_id segment) : begin(segment, 0, 0), end(segment + 1, 0, 0) {}

BlobLocationRange::BlobLocationRange(segment_id segment, cluster_id cluster) : begin(segment, cluster, 0), end(segment, cluster + 1, 0) {}

BlobLocationRange::BlobLocationRange(segment_id segment, cluster_id cluster, blob_id blob) : begin(segment, cluster, blob), end(segment, cluster, blob + 1) {}

BlobLocationRange::BlobLocationRange(segment_id segment, cluster_id cluster, blob_id blobBegin, blob_id blobEnd) : begin(segment, cluster, blobBegin), end(segment, cluster, blobEnd) {}

bool BlobLocationRange::IsCreatedCluster() const { 
  return end.cluster == begin.cluster + 1;
}

bool BlobLocationRange::IsCreatedSegment() const { 
  return end.segment == begin.segment + 1; 
}



bool BlobLocationRanges::Encompasses(const BlobLocation& location) const {
  return std::any_of(begin(), end(), [&](const BlobLocationRange& range) { return range.begin <= location && location < range.end; });
}

bool BlobLocationRanges::Encompasses(const BlobLocationRange& checkRange) const {
  return std::any_of(begin(), end(), [&](const BlobLocationRange& range) { return range.begin <= checkRange.begin && range.end >= checkRange.end; });
}


bool BlobLocationRanges::EncompassesCreatedCluster(segment_id segment, cluster_id cluster) const {
  return std::any_of(begin(), end(), [=](const BlobLocationRange& range) { return range.begin.segment == segment && range.begin.cluster == cluster && range.IsCreatedCluster(); });
}

bool BlobLocationRanges::EncompassesCreatedSegment(segment_id segment) const {
  return std::any_of(begin(), end(), [=](const BlobLocationRange& range) { return range.begin.segment == segment && range.IsCreatedSegment(); });
}


void BlobLocationRanges::Enter(const BlobLocationRange& range) {
  if (!ranges.empty() && ranges.back().end == range.begin) {
    // join the two ranges together instead of adding a new one
    ranges.back().end = range.end;
  } else {
    ranges.push_back(range);
  }
}


void BlobLocationRanges::EnterNewCluster(segment_id segmentId, cluster_id clusterId) {
  // First blob 0 is implicitly created
  ranges.push_back({ segmentId, clusterId, 0 });

  // Then all special blobs at the end of the cluster are also implicitly created
  BlobLocationRange range;
  range.begin = BlobLocation(segmentId, clusterId, constants::MaxBlobId + 1);
  range.end = BlobLocation(segmentId, clusterId + 1, 0);
  ranges.push_back(range);
}

void BlobLocationRanges::EnterNewSegment(segment_id segmentId) {
  // Cluster 0 is implicitly created
  EnterNewCluster(segmentId, 0);

  // The special clusterids are also implicitly created
  BlobLocationRange range;
  range.begin = BlobLocation(segmentId, constants::MaxClusterId + 1, 0);
  range.end = BlobLocation(segmentId + 1, 0, 0);
  ranges.push_back(range);
}

BlobLocationRanges::iterator BlobLocationRanges::begin() { 
  return ranges.begin(); 
}

BlobLocationRanges::iterator BlobLocationRanges::end() { 
  return ranges.end(); 
}

BlobLocationRanges::const_iterator BlobLocationRanges::begin() const {
  return ranges.begin();
}

BlobLocationRanges::const_iterator BlobLocationRanges::end() const {
  return ranges.end();
}



void BlobCreationRanges::NextFreeBlobIdCommitted(segment_id segment, cluster_id cluster, blob_id nextFreeBlobId) {
  committedNextFreeBlobIds.emplace_back(segment, cluster, nextFreeBlobId);
}


bool BlobCreationRanges::TryCreateBlob(const BlobLocation& location) {
  auto pos = std::find_if(committedNextFreeBlobIds.begin(), committedNextFreeBlobIds.end(), [&](auto& entry) { return entry.segment == location.segment && entry.cluster == location.cluster; });
  if (pos == committedNextFreeBlobIds.end() || pos->blob <= location.blob) {
    // No matching NextFreeBlobId committed or committed blob id is higher than the committed NextFreeBlobId
    return false;
  }

  // Mark blob as created
  Enter(BlobLocationRange(location.segment, location.cluster, location.blob));
  return true;
}



bool BlobCreationRanges::EncompassesWithBlobCreation(const BlobLocation& location, Database& database) {
  if (Encompasses(location)) {
    return true;
  }

  if (EncompassesCreatedCluster(location.segment, location.cluster)) {
    // Cluster does not exist, but it has been created by the client during this commit
    // If the client committed a matching NextFreeBlobId then we can also grant the lock
    return TryCreateBlob(location);
  } else if (auto cluster = database.GetLoadedCluster(location.segment, location.cluster)) {
    FIXME("This codepath probably slows down the commit of lots of default created blobs...");
    FIXME("We could track the previous NextFreeBlobId as well to be able to perform a quicker check whether the creation request is valid");

    // The cluster exists
    // If the blob doesn't yet exist, but the client committed a matching NextFreeBlobId then he also implicitly holds the matching lock
    return !cluster->GetBlob(location.blob) && TryCreateBlob(location);
  }

  return false;
}



}