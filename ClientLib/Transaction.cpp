#include "pch.hpp"
#include <blobs/Transaction.hpp>
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
#include <common/BlobLocation.hpp>
#include <internal/Network.hpp>
#include <network/message/All.hpp>
#include <network/Client.hpp>

#include <set>
#include <map>
#include <algorithm>

namespace blobs {

/** The transaction state is a PIMPL structure holding all transaction relevant state while hiding the implementation from the client of the Transaction class.
 *  The state holds one DatabaseState per database, which has been accessed during the transaction. The DatabaseState holds all locks and written data and upon commit
 *  will be used to contruct the commit message.
 */
struct Transaction::State {
  struct DatabaseState {
    DatabaseState(Database* database) : database(database) {}

    struct HeldLocks {
      std::set<BlobLocation> read, write;
    };

    HeldLocks heldLocks;
    std::map<BlobLocation, std::vector<uint8_t>> writtenBlobs;
    std::set<BlobLocation> deletedBlobs;
    std::set<std::pair<segment_id, cluster_id>> deletedClusters;
    std::set<segment_id> deletedSegments;
    Database* database;


    /** Throws an exception if the segment, cluster or blob has already been marked for deletion
     */
    void EnsureBlobNotDeleted(const BlobLocation& location) const {
      EnsureClusterNotDeleted(location.segment, location.cluster);
      if (deletedBlobs.find(location) != deletedBlobs.end()) {
        throw exception::BlobDeleted();
      }
    }

    /** Throws an exception if the segment or cluster has already been marked for deletion
     */
    void EnsureClusterNotDeleted(segment_id segment, cluster_id cluster) const {
      EnsureSegmentNotDeleted(segment);
      if (deletedClusters.find({ segment, cluster }) != deletedClusters.end()) {
        throw exception::ClusterDeleted();
      }
    }

    /** Throws an exception if the segment has already been marked for deletion
     */
    void EnsureSegmentNotDeleted(segment_id segment) const {
      if (deletedSegments.find(segment) != deletedSegments.end()) {
        throw exception::SegmentDeleted();
      }
    }

    struct BlobToSend {
      BlobLocation location;
      const std::vector<uint8_t>* data;
      blob_size blobSize;  // the blob size to transmit, which may differ from the actual size for blobs marked for deletion (see constants::DeleteBlobSize)

      size_t DataSize() const { return data ? data->size() : 0; }
    };



    /** Constructs a sorted list of blob writes to transmit to correctly perform the commit.
     *  This will process all written/deleted blobs/clusters/segments and create a write instruction for all of them and sort them
     *  in the following order to ensure sane transaction processing on the server:
     *   1. Segment creation
     *   1.a Everything else sorted by segment id
     *   2. Cluster creation
     *   2.a Everything else sorted by cluster id
     *   3. Blob creation
     *   4. Blob writes sorted by blob id
     *   5. Blob deletion (this is indicated by an empty blob data and constants::DeleteBlobSize as blobSize member)
     *   6. Cluster deletion
     *   7. Segment deletion
     */
    std::vector<BlobToSend> GetSortedWritesToTransmit() const {
      std::vector<BlobToSend> writesToTransmit;

      for (auto& [location, data] : writtenBlobs) {
        // A regular write. We can simply cast the data size to blob_size as Database::WriteInternal() already validated the blob size is < constants::MaxBlobSize
        writesToTransmit.push_back({ location, &data, static_cast<blob_size>(data.size()) });
      }

      // Now sort all the writes according to the first points of the order
      std::stable_sort(writesToTransmit.begin(), writesToTransmit.end(), [](const BlobToSend& a, const BlobToSend& b) {
        // Segment creation is ordered first (there can only be one such write per transaction)
        if (a.location.segment == constants::NextFreeSegmentId) {
          return true;
        } else if (b.location.segment == constants::NextFreeSegmentId) {
          return false;
        }

        // Then we can order everything by segment
        if (a.location.segment != b.location.segment) {
          return a.location.segment < b.location.segment;
        }

        // Within the segment we will order cluster creations first (only one such write can exist per segment per transaction)
        if (a.location.cluster == constants::NextFreeClusterId) {
          return true;
        } else if (b.location.cluster == constants::NextFreeClusterId) {
          return false;
        }

        // Then we can order everything by cluster
        if (a.location.cluster != b.location.cluster) {
          return a.location.cluster < b.location.cluster;
        }

        // Within a cluster we will order blob creations first (only one such write can exist per cluster per transaction)
        if (a.location.blob == constants::NextFreeBlobId) {
          return true;
        } else if (b.location.blob == constants::NextFreeBlobId) {
          return false;
        }

        // All blob writes within the cluster by blob id
        return a.location.blob < b.location.blob;
      });



      for (auto& location : deletedBlobs) {
        // Blob deletion is indicated by using a special DeleteBlobSize constant as size value while transmitting no data in the blob itself
        writesToTransmit.push_back({ location, nullptr, constants::DeleteBlobSize });
      }

      for (auto [segment, cluster] : deletedClusters) {
        // Cluster deletion is indicated by performing an empty write into the ClusterDeleteId blob of the cluster
        writesToTransmit.push_back({ BlobLocation(segment, cluster, constants::ClusterDeleteId), nullptr, 0 });
      }

      for (auto segment : deletedSegments) {
        // Segment deletion is indicated by perofrming an empty write into SegmentDeleteId cluster
        writesToTransmit.push_back({ BlobLocation(segment, constants::SegmentDeleteId, constants::ClusterDeleteId), nullptr, 0 });
      }

      return writesToTransmit;
    }


    /** Constructs all the commit messages for this database
     */
    void ConstructCommitMessages(database_id dbId, std::vector<network::MessagePointer_T<network::message::TransactionCommit>>& result) const {
      using namespace network::message;
      
      
      // Fetch the sorted write operations to perform
      auto writesToTransmit = GetSortedWritesToTransmit();      

      // Now split up the write operations into TransactionCommit messages by 
      // determining the maximum range of writes [transmitBegin;transmitEnd) that fit into a single TransactionCommit message until
      // we processed all of them.
      for (auto transmitBegin = writesToTransmit.begin(), end = writesToTransmit.end(); transmitBegin != end;) {
        // Sum up the message size and number of blobs for the current message
        size_t totalBlobSize = 0;
        size_t nBlobs = 0;

        // Determine the last blob we can still send in one message
        auto transmitEnd = transmitBegin;
        while (transmitEnd != end && TransactionCommit::IsValidMessageSize(totalBlobSize + transmitEnd->DataSize(), nBlobs + 1)) {
          totalBlobSize += transmitEnd->DataSize();
          ++nBlobs;
          ++transmitEnd;
        }
        
        // Now construct a message for the found range. The range should never be empty, because we already checked in the loop head
        // that there is at least one blob to transmit and we ensure that the blobs are small enough so that each message can hold at least one blob
        assert(transmitBegin != transmitEnd);
        auto commitMessage = TransactionCommit::Create(dbId, totalBlobSize, nBlobs);
        
        // Now write the blobs into the transaction commit message
        auto commitWritePos = commitMessage->begin();
        for (auto transmitPos = transmitBegin; transmitPos != transmitEnd; ++transmitPos, ++commitWritePos) {
          auto& writeEntry = *transmitPos;

          // Set the blob header
          *commitWritePos = writeEntry.location;
          commitWritePos->blobSize = writeEntry.blobSize; // blobSize may be != data->size() for constants::DeleteBlobSize

          // Write the blob data to transmit (if any)
          if (writeEntry.DataSize() > 0) {
            commitWritePos.WriteData(std::string_view(reinterpret_cast<const char*>(writeEntry.data->data()), writeEntry.data->size()));
          }
        }

        assert(commitWritePos == commitMessage->end()); // Otherwise we have miscalculated something!
        
        // Add the completed commit message to the list of commit messages
        result.push_back(std::move(commitMessage));

                
        // Continue after the just processed blob range
        transmitBegin = transmitEnd;
      }
    }
  };

  /** Constructs the commit messages for all databases in this transaction state.
   */
  std::vector<network::MessagePointer_T<network::message::TransactionCommit>> ConstructCommitMessages() const {
    std::vector<network::MessagePointer_T<network::message::TransactionCommit>> commitMessages;

    for (auto& [dbId, state] : forDatabase) {
      state.ConstructCommitMessages(dbId, commitMessages);
    }

    if (!commitMessages.empty()) {
      // Set the follow message property to all but the last commit message for that client
      for (auto pos = commitMessages.begin(), end = commitMessages.end(); pos != end;) {
        (*pos)->hasFollowMessage = (++pos != end);
      }
    }

    return commitMessages;
  }

  /** Fetch or initialize the transaction state for the specified database, which will be used to keep track of
   *  all writes during the transaction and will be used to construct the commit message from
   */
  DatabaseState& AccessDatabaseState(Database* database) {
    auto pos = forDatabase.find(database->id);
    if (pos == forDatabase.end()) {
      pos = forDatabase.emplace(database->id, database).first;
    }
    return pos->second;
  }

  /** State getter, which will only return an existing transaction state, not instantiate a new one and it thus also doesn't need 
   *  a full Database.
   */
  DatabaseState* GetDatabaseState(database_id dbId) {
    auto pos = forDatabase.find(dbId);
    return (pos != forDatabase.end()) ? &pos->second : nullptr;
  }


  std::map<database_id, DatabaseState> forDatabase;
};

uint64_t Transaction::nextId = 0;
std::map<connection_id, Transaction> Transaction::active;

Transaction::Transaction(connection_id connectionId) : id(nextId++), state(new State), connectionId(connectionId) {}


bool Transaction::IsRunning() {
  return !active.empty();
}

bool Transaction::Commit() {
  if (!IsRunning()) {
    return false;
  }

  std::vector<std::pair<network::Client*, Transaction*>> waitForReplies;


  // Construct the commit messages for each server connection
  for (auto& [connectionId, transaction] : active) {
    auto commitMessages = transaction.state->ConstructCommitMessages();
    if (!commitMessages.empty()) {

      // Send all commit messages to the client
      auto& client = internal::Network::Get(connectionId);
      for (auto& message : commitMessages) {
        TODO("Implement a batch send to send a range of messages to the server to avoid synchronization overhead");
        client.SendMessageToServer(std::move(message));
      }

      // We sent a commit message so note down that we have to wait for a commit response
      waitForReplies.push_back({ &client, &transaction });
    }
  }


  TODO("Collect errors into a single error message and throw the error at the end (after reseting all state)");
  for (auto [client, transaction] : waitForReplies) {
    auto response = client->AwaitMessage();

    if (auto commitResponse = response.Get<network::message::TransactionCommitResponse>()) {
      if (commitResponse->result == network::message::TransactionCommitResponse::Result::SUCCESS) {
        // Update the cached versions of all written blobs for all databases involved in the commit
        for (auto& commitEntry : *commitResponse) {
          auto dbState = transaction->state->GetDatabaseState(commitEntry.dbId);
          assert(dbState != nullptr); // Something is seriously wrong if we just committed data from a database state, which is now gone (or server messed up)
          auto database = dbState->database;

          // Update each written blob's data in the database cache now
          // Since writtenBlobs does NOT contain the special deletion blobs, we don't have to perform any filtering for these ids
          for (auto& [location, data] : dbState->writtenBlobs) {
            // Since we don't need the data in the writtenBlobs anymore, we can safely just move it into the cache to 
            // save some reallocations
            database->UpdateCacheForCommittedBlob(location, std::move(data), commitEntry.commitId, transaction->id);
          }

          TODO("Maybe also delete all deleted blobs from the cache?");
          TODO("We should probably also clear some old unused cache entries here, but how old is too old?");
        }
      } else {
        assert(false);
        TODO("Convert error code into some nice error message and include the database in there somehow");
      }
    } else {
      assert(false);
      TODO("It could also be a connection close message... handle it accordingly");
      TODO("All other replies should return an unexpected server message exception");
    }
  }


  // Reset all transaction state for all connections
  active.clear();

  TODO("Throw error if any");

  return true;
}


bool Transaction::Abort() {
  if (!IsRunning()) {
    return false;
  }

  // Send a transaction abort message to all server connections where we have active connections
  for (auto& [connectionId, transaction] : active) {
    auto& client = internal::Network::Get(connectionId);
    client.SendMessageToServer(network::message::TransactionAbort::Create());
  }

  // Then delete all transaction state to prevent any future commit if the aborted transaction
  active.clear();

  
  // We don't expect a confirmation from the server for a transaction abort message, which is fine since
  // the server won't randomly drop messages or receive them out of order.
  return true;
}


void Transaction::AbortDeadlock() {
  // Notify all other database servers with active connections to abort them
  for (auto& [connectionId, transaction] : active) {
    if (connectionId != this->connectionId) {
      auto& client = internal::Network::Get(connectionId);
      client.SendMessageToServer(network::message::TransactionAbort::Create());
      // We don't expect any response to the abort message there is nothing to wait for
    }
  }

  // Finally clear all transaction state (This will delete `this`!)
  Transaction::active.clear();
}


Transaction* Transaction::Get(connection_id connectionId, bool startIfNotActive) {
  auto pos = active.find(connectionId);

  if (pos == active.end() && startIfNotActive) {
    pos = active.emplace(connectionId, Transaction(connectionId)).first;
  }

  return (pos != active.end()) ? &pos->second : nullptr;
}


Transaction::LockMode Transaction::GetLockType(Database* database, const BlobLocation& location) const {
  if (auto dbState = state->GetDatabaseState(database->id)) {
    auto& locks = dbState->heldLocks;
    if (locks.write.find(location) != locks.write.end()) {
      return LockMode::Write;
    } 

    if (locks.read.find(location) != locks.read.end()) {
      return LockMode::Read;
    }
  }

  TODO("Handle special lock values... If we hold a write lock on cluster creation, then we also have an implicit write lock on all created clusters");
  TODO("Maybe we should instead hold lock ranges, instead of single granular locks?")

  return LockMode::None;
}

void Transaction::AcquiredLock(Database* database, const BlobLocation& location, LockMode lock) {
  assert(lock != LockMode::None); // Why would anyone call it like this!?
  auto& dbLocks = state->AccessDatabaseState(database).heldLocks;
  
  // We simply insert without checking for any invariants. Having both a write and read lock will be recognized as simply
  // having a write lock by GetLockType() we thus don't have to worry about removing a read lock when upgrading to a write lock
  if (lock == LockMode::Read) {
    dbLocks.read.insert(location);
  } else if (lock == LockMode::Write) {
    dbLocks.write.insert(location);
  }
}



void Transaction::WriteBlob(Database* database, const BlobLocation& location, const void* blobData, blob_size blobSize) {
  auto& dbState = state->AccessDatabaseState(database);
  
  // Make sure, the blob hasn't already been marked for deletion
  dbState.EnsureBlobNotDeleted(location);
  
  // Store the new blob data in our transaction state
  auto& blobVector = dbState.writtenBlobs[location];
  blobVector.resize(blobSize);
  std::copy_n(static_cast<const uint8_t*>(blobData), blobSize, blobVector.begin());
}


void Transaction::DeleteBlob(Database* database, const BlobLocation& location) {
  auto& dbState = state->AccessDatabaseState(database);

  // Make sure, the blob hasn't already been marked for deletion
  dbState.EnsureBlobNotDeleted(location);


  // If we first called WriteBlob() and then in the same transaction DeleteBlob(),
  // then clear the write data and mark the blob as deleted.
  dbState.writtenBlobs.erase(location);

  // Mark the blob as deleted
  dbState.deletedBlobs.insert(location);
}


void Transaction::DeleteCluster(Database* database, segment_id segment, cluster_id cluster) {
  auto& dbState = state->AccessDatabaseState(database);

  // Make sure, the cluster hasn't already been marked for deletion
  dbState.EnsureClusterNotDeleted(segment, cluster);

  // Now discard all write/delete operations in that cluster as the whole cluster will be discarded anyway
  auto writtenBlobsBegin = dbState.writtenBlobs.lower_bound(BlobLocation(segment, cluster, 0));
  auto writtenBlobsEnd = dbState.writtenBlobs.upper_bound(BlobLocation(segment, cluster, std::numeric_limits<blob_id>::max()));
  dbState.writtenBlobs.erase(writtenBlobsBegin, writtenBlobsEnd);


  auto deletedBlobsBegin = dbState.deletedBlobs.lower_bound(BlobLocation(segment, cluster, 0));
  auto deletedBlobsEnd = dbState.deletedBlobs.upper_bound(BlobLocation(segment, cluster, std::numeric_limits<blob_id>::max()));
  dbState.deletedBlobs.erase(deletedBlobsBegin, deletedBlobsEnd);


  // Finally note down that we deleted it (we don't construct a writtenBlob entry for ClusterDeleteId, that is handled in commit())
  dbState.deletedClusters.insert({ segment, cluster });
}


void Transaction::DeleteSegment(Database* database, segment_id segment) {
  auto& dbState = state->AccessDatabaseState(database);

  // Make sure, the segment hasn't already been marked for deletion
  dbState.EnsureSegmentNotDeleted(segment);


  // Now discard all write/delete operations in that segment as the whole segment will be discarded anyway
  auto writtenBlobsBegin = dbState.writtenBlobs.lower_bound(BlobLocation(segment, 0, 0));
  auto writtenBlobsEnd = dbState.writtenBlobs.upper_bound(BlobLocation(segment, std::numeric_limits<cluster_id>::max(), std::numeric_limits<blob_id>::max()));
  dbState.writtenBlobs.erase(writtenBlobsBegin, writtenBlobsEnd);


  auto deletedBlobsBegin = dbState.deletedBlobs.lower_bound(BlobLocation(segment, 0, 0));
  auto deletedBlobsEnd = dbState.deletedBlobs.upper_bound(BlobLocation(segment, std::numeric_limits<cluster_id>::max(), std::numeric_limits<blob_id>::max()));
  dbState.deletedBlobs.erase(deletedBlobsBegin, deletedBlobsEnd);

  auto deletedClustersBegin = dbState.deletedClusters.lower_bound({ segment, 0 });
  auto deletedClustersEnd = dbState.deletedClusters.upper_bound({ segment, std::numeric_limits<cluster_id>::max() });
  dbState.deletedClusters.erase(deletedClustersBegin, deletedClustersEnd);


  // Finally note down that we deleted it (we don't construct a writtenBlob entry for SegmentDeleteId, that is handled in commit())
  dbState.deletedSegments.insert(segment);
}



std::optional<std::pair<const void*, blob_size>> Transaction::ReadBlob(Database* database, const BlobLocation& location) const {
  if (auto dbState = state->GetDatabaseState(database->id)) {
  
    // Make sure, we don't attempt to read from a blob marked for deletion
    dbState->EnsureBlobNotDeleted(location);

    auto writePos = dbState->writtenBlobs.find(location);
    if (writePos != dbState->writtenBlobs.end()) {
      // Blob has been written in this transaction -> return the written content
      return std::make_pair(static_cast<const void*>(writePos->second.data()), static_cast<blob_size>(writePos->second.size()));
    }
  }

  // Blob not written in this transaction yet (or database not touched yet during this transaction)
  return std::nullopt;
}



}

