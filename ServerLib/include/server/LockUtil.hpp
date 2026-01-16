#pragma once

#include <network/message/BlobsRead.hpp>
#include "Database.hpp"

#include <cassert>

namespace blobs::server {


/** Handles special blob locations and determines all blocks, which need to be locked to access the given blob location
 *  and evaluates the predicate on each one of them.
 *
 * @param database the database to load the cluster from if necessary
 * @param predicate(const BlobLocation&) -> bool
 *
 * @return true if the predicate returned true for all locations, false otherwise
 */
template<typename Predicate>
bool AllLocksForLocation(Database& database, const BlobLocation& location, Predicate&& predicate) {
  if (location.blob == constants::ClusterDeleteId) {
    // For deletion of a cluster, the client must hold locks to every single blob in that cluster
    auto cluster = database.GetLoadedCluster(location.segment, location.cluster);
    assert(cluster); // the caller should ensure the cluster's existence

    if (!predicate(location)) {
      return false;
    }

    TODO("Once we support reading all blobs of a cluster, we must also lock that id");

    if (!predicate(BlobLocation(location.segment, location.cluster, constants::NextFreeBlobId))) {
      return false;
    }

    for (auto& [blobId, blob] : *cluster) {
      if (!predicate(BlobLocation(location.segment, location.cluster, blobId))) {
        return false;
      }
    }

  } else {
    // Regular single blob lock
    if (!predicate(location)) {
      return false;
    }
  }
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
    if (location.blob == constants::ClusterDeleteId) {
      auto cluster = database.GetLoadedCluster(location.segment, location.cluster);
      assert(cluster); // the caller should ensure the cluster's existence

      action(location);
      action(BlobLocation(location.segment, location.cluster, constants::NextFreeBlobId));
      TODO("Once we support reading all blobs of a cluster, we must also lock that id");

      for (auto& [blobId, blob] : *cluster) {
        action(BlobLocation(location.segment, location.cluster, blobId));
      }
    } else {
      // Regular single blob lock
      action(location);
    }
  }
}



}
