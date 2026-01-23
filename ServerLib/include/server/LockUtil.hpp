#pragma once

#include <network/message/BlobsRead.hpp>
#include "Database.hpp"

#include <cassert>

namespace blobs::server {


/** Handles special blob locations and determines all blocks, which need to be locked to access the given blob location
 *  and evaluates the action on each one of them.
 *
 *  This function encapuslates the additional logic for special blobs, like ClusterDeleteId, which require
 *  a lock on the whole cluster, which is also what the database reference is needed for.
 *
 * @param database the database to load the cluster/segment from if necessary
 * @param location the location to check the locks for
 * @param action(const BlobLocation&) -> void
 */
template<typename Action>
void ForEachLockForLocation(Database& database, const BlobLocation& location, Action&& action) {
  if (location.blob == constants::ClusterDeleteId) {
    if (location.cluster == constants::SegmentDeleteId) {
      // Whole segment is being deleted
      auto segment = database.GetLoadedSegment(location.segment);
      assert(segment); // the caller should ensure the segments's existence

      action(location);
      action(BlobLocation(location.segment, constants::NextFreeClusterId, constants::NextFreeBlobId));
      action(BlobLocation(location.segment, constants::ClusterListId, constants::BlobListId)); // The list of clusters in this segment must be locked
      action(BlobLocation(constants::SegmentListId, constants::ClusterListId, constants::BlobListId)); // The list of segments must be locked too

      for (auto& [clusterId, clusterObj] : *segment) {
        // We need to hold all the locks for all clusters in that segment
        // We cannot perform a recursive call here with ClusterDeleteId
        // because we would call action multiple times for the segment's list of clusters if we were to do this
        auto cluster = database.GetLoadedCluster(location.segment, clusterId);

        action(BlobLocation(location.segment, location.cluster, constants::ClusterDeleteId));
        action(BlobLocation(location.segment, location.cluster, constants::NextFreeBlobId));
        action(BlobLocation(location.segment, location.cluster, constants::BlobListId)); // The cluster's blob list

        for (auto& [blobId, blob] : *cluster) {
          action(BlobLocation(location.segment, location.cluster, blobId));
        }
      }
    } else {
      // Whole cluster is being deleted
      auto cluster = database.GetLoadedCluster(location.segment, location.cluster);
      assert(cluster); // the caller should ensure the cluster's existence

      action(location);
      action(BlobLocation(location.segment, location.cluster, constants::NextFreeBlobId));
      action(BlobLocation(location.segment, location.cluster, constants::BlobListId)); // The cluster's blob list
      action(BlobLocation(location.segment, constants::ClusterListId, constants::BlobListId)); // The list of clusters in that segment must be locked too

      for (auto& [blobId, blob] : *cluster) {
        action(BlobLocation(location.segment, location.cluster, blobId));
      }
    }
  } else if (location.blob == constants::NextFreeBlobId) {
    if (location.cluster == constants::NextFreeClusterId) {
      if (location.segment == constants::NextFreeSegmentId) {
        // When attempting to create a segment, we must also lock the segment id list
        action(location);
        action(BlobLocation(constants::SegmentListId, constants::ClusterListId, constants::BlobListId));
      } else {
        // When attempting to create a new cluster, we must also lock the cluster id list of the segment
        action(location);
        action(BlobLocation(location.segment, constants::ClusterListId, constants::BlobListId));
      }
    } else {
      // When attempting to create a new blob, we must also lock the blob id list of that cluster
      action(location);
      action(BlobLocation(location.segment, location.cluster, constants::BlobListId));
    }
  } else {
    // Regular single blob lock
    action(location);
  }
}

/** Handles special blob locations and determines all blocks, which need to be locked to access the given blob location
 *  and evaluates the predicate on each one of them.
 *
 * @param database the database to load the cluster/segment from if necessary
 * @param location the location to check the locks for
 * @param predicate(const BlobLocation&) -> bool
 *
 * @return true if the predicate returned true for all locations, false otherwise
 */
template<typename Predicate>
bool AllLocksForLocation(Database& database, const BlobLocation& location, Predicate&& predicate) {
  if (location.blob == constants::ClusterDeleteId) {
    if (location.cluster == constants::SegmentDeleteId) {
      // For deletion of a segment, the client must hold locks to every single blob in that segment
      auto segment = database.GetLoadedSegment(location.segment);
      assert(segment); // the caller should ensure the segments's existence

      if (!predicate(location)) {
        return false;
      }
      
      if (!predicate(BlobLocation(location.segment, constants::NextFreeClusterId, constants::NextFreeBlobId))) {
        return false;
      }

      // Lock the segments cluster list
      if (!predicate(BlobLocation(location.segment, constants::ClusterListId, constants::BlobListId))) {
        return false;
      }

      // When deleting a segment we must lock the list of all segments
      if (!predicate(BlobLocation(constants::SegmentListId, constants::ClusterListId, constants::BlobListId))) {
        return false;
      }
      
      for (auto& [clusterId, clusterObj] : *segment) {
        // We need to hold all the locks for all the clusters
        // We cannot perform a recursive call here with ClusterDeleteId as this would call the predicate multiple times
        // on the segment's cluster list
        auto cluster = database.GetLoadedCluster(location.segment, location.cluster);

        if (!predicate(BlobLocation(location.segment, location.cluster, constants::ClusterDeleteId))) {
          return false;
        }

        if (!predicate(BlobLocation(location.segment, location.cluster, constants::NextFreeBlobId))) {
          return false;
        }

        // The cluster's blob list
        if (!predicate(BlobLocation(location.segment, location.cluster, constants::BlobListId))) {
          return false;
        }
      }
    } else {
      // For deletion of a cluster, the client must hold locks to every single blob in that cluster
      auto cluster = database.GetLoadedCluster(location.segment, location.cluster);
      assert(cluster); // the caller should ensure the cluster's existence

      if (!predicate(location)) {
        return false;
      }

      if (!predicate(BlobLocation(location.segment, location.cluster, constants::NextFreeBlobId))) {
        return false;
      }

      // The cluster's blob list
      if (!predicate(BlobLocation(location.segment, location.cluster, constants::BlobListId))) {
        return false;
      }

      // When deleting a cluster, we must lock the segments list of all clusters
      if (!predicate(BlobLocation(location.segment, constants::ClusterListId, constants::BlobListId))) {
        return false;
      }

      for (auto& [blobId, blob] : *cluster) {
        if (!predicate(BlobLocation(location.segment, location.cluster, blobId))) {
          return false;
        }
      }
    }
  } else if (location.blob == constants::NextFreeBlobId) {
    if (location.cluster == constants::NextFreeClusterId) {
      if (location.segment == constants::NextFreeSegmentId) {
        // When attempting to create a segment, we must also lock the segment id list
        if (!predicate(location)) {
          return false;
        }

        if (!predicate(BlobLocation(constants::SegmentListId, constants::ClusterListId, constants::BlobListId))) {
          return false;
        }
      } else {
        // When attempting to create a new cluster, we must also lock the cluster id list of the segment
        if (!predicate(location)) {
          return false;
        }

        if (!predicate(BlobLocation(location.segment, constants::ClusterListId, constants::BlobListId))) {
          return false;
        }
      }
    } else {
      // When attempting to create a new blob, we must also lock the blob id list of that cluster
      if (!predicate(location)) {
        return false;
      }

      if (!predicate(BlobLocation(location.segment, location.cluster, constants::BlobListId))) {
        return false;
      }
    }
  } else {
    // Regular single blob lock
    if (!predicate(location)) {
      return false;
    }
  }

  return true;
}


/** Returns true if the given predicate is true for all required locks of the blobs read message.
 *  Iteration stops on the first location for which the predicate returns false.
 * 
 *  This function encapuslates the additional logic for special blobs, like ClusterDeleteId, which require 
 *  a lock on the whole cluster, which is also what the database reference is needed for.
 *
 * @param database the database to load the cluster from in case we need to iterate over all its blobs
 * @param message the message whose locations should be iterated over
 * @param predicate(const BlobLocation&) -> bool
 * 
 * @return true if the predicate returned true for all lock locations, false otherwise
 */
template<typename Pred>
bool AllLocksInMessage(Database& database, const network::message::BlobsRead& message, Pred&& predicate) {
  for (auto& location : message) {
    if (!AllLocksForLocation(database, location, predicate)) {
      return false;
    }
  }

  return true;
}



/** Executes the given action on each required lock of the blobs read message.
 *
 *  This function encapuslates the additional logic for special blobs, like ClusterDeleteId, which require 
 *  a lock on the whole cluster, which is also what the database reference is needed for.
 * 
 * @param database the database to load the cluster from if necessary
 * @param message the message whose locks should be iterated
 * @param action(const BlobLocation&) -> void
 */
template<typename Action>
void ForEachLockInMessage(Database& database, const network::message::BlobsRead& message, Action&& action) {
  for (auto& location : message) {
    ForEachLockForLocation(database, location, action);
  }
}



}
