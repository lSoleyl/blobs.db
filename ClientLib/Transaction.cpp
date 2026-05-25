#include "pch.hpp"
#include <blobs/Transaction.hpp>
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
#include <common/BlobLocation.hpp>
#include <internal/Network.hpp>
#include <internal/HeldLocks.hpp>
#include <internal/TransactionsState.hpp>
#include <internal/DatabasesState.hpp>
#include <network/message/All.hpp>
#include <network/ClientInterface.hpp>

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
    DatabaseState(Database* database, std::unique_ptr<internal::HeldLocks> stickyLocks) : database(database), heldLocks(std::move(stickyLocks)) {
      if (!heldLocks) {
        // No sticky locks passed from previous transaction
        heldLocks.reset(new internal::HeldLocks);
      }
    }

    

    std::unique_ptr<internal::HeldLocks> heldLocks;
    std::map<BlobLocation, std::vector<uint8_t>> writtenBlobs;
    std::set<BlobLocation> deletedBlobs;
    std::set<BlobLocation> createdBlobs;
    std::set<std::pair<segment_id, cluster_id>> deletedClusters;
    std::set<std::pair<segment_id, cluster_id>> createdClusters;
    std::set<segment_id> deletedSegments;
    std::set<segment_id> createdSegments;
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

    /** Returns true if the blob, its cluster or segment is marked for deletion
     */
    bool IsBlobDeleted(const BlobLocation& location) const {
      return IsClusterDeleted(location.segment, location.cluster) || deletedBlobs.count(location) != 0;
    }

    /** Returns true if the specified cluster or its segment is marked for deletion
     */
    bool IsClusterDeleted(segment_id segment, cluster_id cluster) const {
      return IsSegmentDeleted(segment) || deletedClusters.count({ segment, cluster }) != 0;
    }

    /** Returns true if the specified segment is marked for deletion
     */
    bool IsSegmentDeleted(segment_id segment) const {
      return deletedSegments.count(segment) != 0;
    }

    /** Returns true if the given blob, its cluster or its segment have been created in this transaction
     */
    bool IsBlobCreated(const BlobLocation& location) const {
      return createdBlobs.count(location) != 0 || IsClusterCreated(location.segment, location.cluster);
    }

    /** Returns true if the given cluster or its segment have been created in this transaction
     */
    bool IsClusterCreated(segment_id segment, cluster_id cluster) const {
      return createdClusters.count({ segment, cluster }) != 0 || IsSegmentCreated(segment);
    }

    /** Returns true if the given segment has been created in this transaction
     */
    bool IsSegmentCreated(segment_id segment) const {
      return createdSegments.count(segment) != 0;
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

    /** Releases locks held on blobs,cluster,segments, which were deleted during this transaction as they are no longer valid.
     */
    void ReleaseDeletedBlobLocks() {
      // First release all locks on deleted segments
      for (auto segmentId : deletedSegments) {
        BlobLocation segmentBegin(segmentId, 0, 0);
        BlobLocation segmentEnd(segmentId, std::numeric_limits<cluster_id>::max(), std::numeric_limits<blob_id>::max());
        heldLocks->ReleaseLockRange(segmentBegin, segmentEnd);
      }

      // Then release all locks held on deleted clusters
      for (auto [segmentId, clusterId] : deletedClusters) {
        BlobLocation clusterBegin(segmentId, clusterId, 0);
        BlobLocation clusterEnd(segmentId, clusterId, std::numeric_limits<blob_id>::max());
        heldLocks->ReleaseLockRange(clusterBegin, clusterEnd);
      }

      // Finally relase locks held on all deleted single blobs
      for (auto& location : deletedBlobs) {
        heldLocks->ReleaseLockRange(location, BlobLocation(location.segment, location.cluster, location.blob + 1));
      }
    }

    /** This method will clear the corresponding id list from the database cache for each created/deleted blob/cluster/segment.
     *  This is used to avoid holding on to outdated id lists.
     */
    void ClearModifiedIdListsInCache() {
      FIXME(
        "Instead of simply clearing the outdated id list blobs from the cache, we could actually update them here to their new state and keep them!"
        "That way we could save re-requesting the blob in the next transaction from the server (if the client wants to read it in the next transaction)"
      );

      for (auto& location : createdBlobs) {
        database->RemoveCachedBlob(BlobLocation(location.segment, location.cluster, constants::BlobListId));
      }

      for (auto& location : deletedBlobs) {
        database->RemoveCachedBlob(BlobLocation(location.segment, location.cluster, constants::BlobListId));
      }

      for (auto [segment, cluster] : createdClusters) {
        database->RemoveCachedBlob(BlobLocation(segment, constants::ClusterListId, constants::BlobListId));
      }

      for (auto [segment, cluster] : deletedClusters) {
        database->RemoveCachedBlob(BlobLocation(segment, constants::ClusterListId, constants::BlobListId));
      }

      if (!deletedSegments.empty() || !createdSegments.empty()) {
        database->RemoveCachedBlob(BlobLocation(constants::SegmentListId, constants::ClusterListId, constants::BlobListId));
      }
    }

  };

  /** Constructs the commit messages for all databases in this transaction state.
   */
  std::vector<network::MessagePointer_T<network::message::TransactionCommit>> ConstructCommitMessages(blobs::internal::DatabasesState& databases) const {
    std::vector<network::MessagePointer_T<network::message::TransactionCommit>> commitMessages;

    for (auto& [dbId, state] : forDatabase) {
      state.ConstructCommitMessages(dbId, commitMessages);
    }

    if (!commitMessages.empty()) {
      // Set the follow message property to all but the last commit message for that client
      for (auto pos = commitMessages.begin(), end = commitMessages.end(); pos != end;) {
        auto& commitMessage = *pos;
        commitMessage->hasFollowMessage = (++pos != end);
      }
    } else {
      // No writes to any database on this server connection.
      // We must still send at least one empty commit message to the server. 
      // Otherwise the server and client's transaction state will get out of sync leading to followup errors.
      // For that we will use the first best database on the client.

      // This cannot happen as it would mean that we started a transaction on a connection with no databases
      // Transactions are always started implicitly when accessing databases
      assert(!databases.openedDatabases.empty()); 

      // Simply generate an empty commit message for the first database we know.
      // The database used is not really relevant, but it should be a valid database as the server checks this.
      // We do not use the forDatabase structure as it may be empty if the transaction has been opened implicitly by a BlobsRead
      // but that operation failed before creating any database state.
      commitMessages.push_back(network::message::TransactionCommit::Create(databases.openedDatabases.begin()->first, 0, 0));
    }

    return commitMessages;
  }

  /** Fetch or initialize the transaction state for the specified database, which will be used to keep track of
   *  all writes during the transaction and will be used to construct the commit message from
   */
  DatabaseState& AccessDatabaseState(Database* database, std::unique_ptr<internal::HeldLocks> stickyLocks = nullptr) {
    auto pos = forDatabase.find(database->id);
    if (pos == forDatabase.end()) {
      pos = forDatabase.emplace(std::piecewise_construct, std::make_tuple(database->id), std::make_tuple(database, std::move(stickyLocks))).first;
    } else {
      assert(!stickyLocks); // Otherwise we are trying to initialize the database's transaction state after it has already been initialized!
    }
    return pos->second;
  }


  /** Releases locks held on blobs,cluster,segments, which were deleted during this transaction as they are no longer valid.
   */
  void ReleaseDeletedBlobLocks() {
    for (auto& [dbId, state] : forDatabase) {
      state.ReleaseDeletedBlobLocks();
    }
  }

  /** Clears the cached id list blobs for all created/deleted blobs/clusters/segments to avoid reading an outdated list from the cache in the next transaction.
   *  The list would be outdated if we hold a sticky lock on the list and we read the list in the previous transaction. Then Database::ApplyStickyLocksToTansaction()
   *  would simply update the transaction id of the cached blob, because it couldn't have possibly changed in between. The only issue is that we currently don't update the
   *  cached id list when we commit changes to the database that would modify the id list. (TODO)
   */
  void ClearModifiedIdListsInCache() {
    for (auto& [dbId, state] : forDatabase) {
      state.ClearModifiedIdListsInCache();
    }
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

Transaction::Transaction(const Session::Handle& session, connection_id connectionId) : id(session->Transactions().nextId++), state(new State), connectionId(connectionId), session(session) {}
Transaction::~Transaction() {}

bool Transaction::IsRunning(const Session::Handle& session) {
  auto sessionLock = session->Lock();
  return session->Transactions().IsRunning();
}

bool Transaction::Commit(const Session::Handle& session) {
  auto sessionLock = session->Lock();
  auto& transactions = session->Transactions();

  if (!transactions.IsRunning()) {
    return false;
  }

  std::vector<std::pair<network::ClientInterface*, Transaction*>> waitForReplies;

  auto& network = session->Network();
  // Construct the commit messages for each server connection
  for (auto& [connectionId, transaction] : transactions.active) {
    auto commitMessages = transaction.state->ConstructCommitMessages(session->Databases(connectionId));

    assert(!commitMessages.empty()); // We must send at least an empty commit message to each server otherwise the states will be out of sync


    // Send all commit messages to the client
    auto& client = network.Get(connectionId);
    for (auto& message : commitMessages) {
      TODO("Implement a batch send to send a range of messages to the server to avoid synchronization overhead");
      client.SendMessageToServer(std::move(message));
    }

    // We sent a commit message so note down that we have to wait for a commit response
    waitForReplies.push_back({ &client, &transaction });
  }


  TODO("Collect errors into a single error message and throw the error at the end (after reseting all state)");
  for (auto [client, transaction] : waitForReplies) {
    auto response = client->AwaitMessage();

    if (auto commitResponse = response.Get<network::message::TransactionCommitResponse>()) {
      if (commitResponse->result == network::message::TransactionCommitResponse::Result::SUCCESS) {
        // Update the cached versions of all written blobs for all databases involved in the commit
        for (auto& commitEntry : *commitResponse) {
          // Update the database cache from the written blobs.
          // We don't necessarily need to have a database transaction state in case we performed an empty commit
          // and just sent an empty TransactionCommit message for the first opened database (otherwise we would have no message to send).
          FIXME("If we send a TransactionAbort instead of empty commits then we could perform more strict checking here and could maybe catch more logic errors");
          if (auto dbState = transaction->state->GetDatabaseState(commitEntry.dbId)) {
            auto database = dbState->database;

            // Update each written blob's data in the database cache now
            // Since writtenBlobs does NOT contain the special deletion blobs, we don't have to perform any filtering for these ids
            for (auto& [location, data] : dbState->writtenBlobs) {
              // Since we don't need the data in the writtenBlobs anymore, we can safely just move it into the cache to 
              // save some reallocations
              database->UpdateCacheForCommittedBlob(location, std::move(data), commitEntry.commitId, transaction->id);
            }

            TODO("We should probably also clear some old unused cache entries here, but how old is too old?");
          }
        }
      } else {
        std::ostringstream error;
        error << "Internal error: Unexpected result in Transaction::Commit(): " << commitResponse->result;
        
        TODO("Recover as good as possible from this scenario before throwing the exception to be able to continue execution with a new transaction");
        TODO("Revoke all locks");
        throw exception::InternalCommitError(error.str());
      }
    } else {
      assert(false);
      TODO("It could also be a connection close message... handle it accordingly");
      TODO("All other replies should return an unexpected server message exception");
    }
  }


  // Release the held locks to any blob,cluster,segment, which we deleted during this transaction as they 
  // lock ressources that no longer exist.
  for (auto& [connectionId, transaction] : transactions.active) {
    transaction.state->ReleaseDeletedBlobLocks();
    transaction.state->ClearModifiedIdListsInCache();
  }


  // Reset all transaction state for all connections
  TransferAndClearState(session);

  TODO("Throw error if any");

  return true;
}


bool Transaction::Abort(const Session::Handle& session) {
  auto sessionLock = session->Lock();
  auto& transactions = session->Transactions();

  if (!transactions.IsRunning()) {
    return false;
  }

  // Send a transaction abort message to all server connections where we have active connections
  auto& network = session->Network();
  for (auto& [connectionId, transaction] : transactions.active) {
    auto& client = network.Get(connectionId);
    client.SendMessageToServer(network::message::TransactionAbort::Create());
  }

  // Then delete all transaction state to prevent any future commit if the aborted transaction
  TransferAndClearState(session);

  
  // We don't expect a confirmation from the server for a transaction abort message, which is fine since
  // the server won't randomly drop messages or receive them out of order.
  return true;
}


bool Transaction::UseStickyLocks(const Session::Handle& session, bool use) {
  auto sessionLock = session->Lock();
  auto& transactions = session->Transactions();
  return std::exchange(transactions.useStickyLocks, use);
}


void Transaction::AbortDeadlock() {
  assert(session->OwnsLock()); // should only be called from inside AwaitMessage(), for which we already need to have a session lock

  auto& network = session->Network();

  // Notify all other database servers with active connections to abort them
  for (auto& [connectionId, transaction] : session->Transactions().active) {
    if (connectionId != this->connectionId) {
      auto& client = network.Get(connectionId);
      client.SendMessageToServer(network::message::TransactionAbort::Create());
      // We don't expect any response to the abort message there is nothing to wait for
    }
  }

  // Finally clear all transaction state (This will delete `this`!)
  TransferAndClearState(session);
}


Transaction* Transaction::Get(const Session::Handle& session, connection_id connectionId) {
  assert(session->OwnsLock());
  auto& active = session->Transactions().active;
  auto pos = active.find(connectionId);
  return (pos != active.end()) ? &pos->second : nullptr;
}

Transaction& Transaction::Create(const Session::Handle& session, connection_id connectionId) {
  assert(session->OwnsLock());
  auto& active = session->Transactions().active;
  return active.emplace(connectionId, Transaction(session, connectionId)).first->second;
}


Transaction::LockMode Transaction::GetLockType(Database* database, const BlobLocation& location) const {
  assert(session->OwnsLock());

  if (auto dbState = state->GetDatabaseState(database->id)) {
    auto& locks = *dbState->heldLocks;
    if (locks.write.find(location) != locks.write.end()) {
      return LockMode::Write;
    } 

    if (locks.read.find(location) != locks.read.end()) {
      return LockMode::Read;
    }
  }

  return LockMode::None;
}


void Transaction::UseStickyLocks(Database* database, std::unique_ptr<internal::HeldLocks> stickyLocks) {
  assert(session->OwnsLock());

  // Initialize the database state and assign the sticky locks to it upon construction
  state->AccessDatabaseState(database, std::move(stickyLocks));
}


void Transaction::AcquiredLock(Database* database, const BlobLocation& location, LockMode lock) {
  assert(session->OwnsLock());
  assert(lock != LockMode::None); // Why would anyone call it like this!?
  auto& dbLocks = *state->AccessDatabaseState(database).heldLocks;
  
  // We simply insert without checking for any invariants. Having both a write and read lock will be recognized as simply
  // having a write lock by GetLockType() we thus don't have to worry about removing a read lock when upgrading to a write lock
  if (lock == LockMode::Read) {
    dbLocks.read.insert(location);
  } else if (lock == LockMode::Write) {
    dbLocks.write.insert(location);
  }
}



void Transaction::WriteBlob(Database* database, const BlobLocation& location, const void* blobData, blob_size blobSize) {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);
  
  // Make sure, the blob hasn't already been marked for deletion
  dbState.EnsureBlobNotDeleted(location);
  
  // Store the new blob data in our transaction state
  auto& blobVector = dbState.writtenBlobs[location];
  blobVector.resize(blobSize);
  std::copy_n(static_cast<const uint8_t*>(blobData), blobSize, blobVector.begin());
}


void Transaction::CreateBlob(Database* database, const BlobLocation& location) {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);

  // We cannot create a blob in a deleted cluster
  assert(!dbState.IsClusterDeleted(location.segment, location.cluster)); 

  if (!dbState.deletedBlobs.erase(location)) {
    // The blob was not deleted in this transaction, so mark it as created
    dbState.createdBlobs.insert(location);
  }
  // Else the blob has been deleted in this same transaction and now re-created... 
  // This is equivalent to simply performing a write on that blob.
  // Since CreateBlobInternal always calls Transaction::WriteBlob() after Transaction::CreateBlob() 
  // we know for sure that this blob will be entered into dbState.writtenBlobs by that call.
  // We cannot enter it here, because we don't know the data to write into the blob.
}

void Transaction::CreateCluster(Database* database, segment_id segment, cluster_id cluster) {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);

  // We cannot create a cluster that has been marked for deletion
  assert(!dbState.IsClusterDeleted(segment, cluster));

  // Mark the cluster as created
  dbState.createdClusters.insert({segment, cluster});
}

void Transaction::CreateSegment(Database* database, segment_id segment) {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);

  // We cannot create a segment that has been marked for deletion
  assert(!dbState.IsSegmentDeleted(segment));

  // Mark the segment as created
  dbState.createdSegments.insert(segment);
}


bool Transaction::IsCreatedBlob(Database* database, const BlobLocation& location) const {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);
  return dbState.IsBlobCreated(location);
}

bool Transaction::IsCreatedCluster(Database* database, segment_id segment, cluster_id cluster) const {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);
  return dbState.IsClusterCreated(segment, cluster);
}

bool Transaction::IsCreatedSegment(Database* database, segment_id segment) const {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);
  return dbState.IsSegmentCreated(segment);
}


void Transaction::DeleteBlob(Database* database, const BlobLocation& location) {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);

  // Make sure, the blob hasn't already been marked for deletion
  dbState.EnsureBlobNotDeleted(location);


  // If we first called WriteBlob() and then in the same transaction DeleteBlob(),
  // then clear the write data and mark the blob as deleted.
  dbState.writtenBlobs.erase(location);

  // If we created the blob in this transaction and then deleted it, then we must
  // remove it from the list of created blobs
  dbState.createdBlobs.erase(location);

  // Mark the blob as deleted
  dbState.deletedBlobs.insert(location);

  // The blob is erased from the cache in the calling Database::DeleteBlob()
}


void Transaction::DeleteCluster(Database* database, segment_id segment, cluster_id cluster) {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);

  // Make sure, the cluster hasn't already been marked for deletion
  dbState.EnsureClusterNotDeleted(segment, cluster);

  // Now discard all write/create/delete operations in that cluster as the whole cluster will be discarded anyway
  auto writtenBlobsBegin = dbState.writtenBlobs.lower_bound(BlobLocation(segment, cluster, 0));
  auto writtenBlobsEnd = dbState.writtenBlobs.upper_bound(BlobLocation(segment, cluster, std::numeric_limits<blob_id>::max()));
  dbState.writtenBlobs.erase(writtenBlobsBegin, writtenBlobsEnd);

  auto createdBlobsBegin = dbState.createdBlobs.lower_bound(BlobLocation(segment, cluster, 0));
  auto createdBlobsEnd = dbState.createdBlobs.upper_bound(BlobLocation(segment, cluster, std::numeric_limits<blob_id>::max()));
  dbState.createdBlobs.erase(createdBlobsBegin, createdBlobsEnd);

  auto deletedBlobsBegin = dbState.deletedBlobs.lower_bound(BlobLocation(segment, cluster, 0));
  auto deletedBlobsEnd = dbState.deletedBlobs.upper_bound(BlobLocation(segment, cluster, std::numeric_limits<blob_id>::max()));
  dbState.deletedBlobs.erase(deletedBlobsBegin, deletedBlobsEnd);

  // If the cluster has been created during this transaction, then we must discard this info as well
  dbState.createdClusters.erase({ segment, cluster });

  // Finally note down that we deleted it (we don't construct a writtenBlob entry for ClusterDeleteId, that is handled in commit())
  dbState.deletedClusters.insert({ segment, cluster });

  // The cluster with all blobs is erased from the cache in the calling Database::DeleteCluster()
}


void Transaction::DeleteSegment(Database* database, segment_id segment) {
  assert(session->OwnsLock());
  auto& dbState = state->AccessDatabaseState(database);

  // Make sure, the segment hasn't already been marked for deletion
  dbState.EnsureSegmentNotDeleted(segment);


  // Now discard all write/create/delete operations in that segment as the whole segment will be discarded anyway
  auto writtenBlobsBegin = dbState.writtenBlobs.lower_bound(BlobLocation(segment, 0, 0));
  auto writtenBlobsEnd = dbState.writtenBlobs.upper_bound(BlobLocation(segment, std::numeric_limits<cluster_id>::max(), std::numeric_limits<blob_id>::max()));
  dbState.writtenBlobs.erase(writtenBlobsBegin, writtenBlobsEnd);

  auto createdBlobsBegin = dbState.createdBlobs.lower_bound(BlobLocation(segment, 0, 0));
  auto createdBlobsEnd = dbState.createdBlobs.upper_bound(BlobLocation(segment, std::numeric_limits<cluster_id>::max(), std::numeric_limits<blob_id>::max()));
  dbState.createdBlobs.erase(createdBlobsBegin, createdBlobsEnd);

  auto deletedBlobsBegin = dbState.deletedBlobs.lower_bound(BlobLocation(segment, 0, 0));
  auto deletedBlobsEnd = dbState.deletedBlobs.upper_bound(BlobLocation(segment, std::numeric_limits<cluster_id>::max(), std::numeric_limits<blob_id>::max()));
  dbState.deletedBlobs.erase(deletedBlobsBegin, deletedBlobsEnd);


  auto createdClustersBegin = dbState.createdClusters.lower_bound({ segment, 0 });
  auto createdClustersEnd = dbState.createdClusters.upper_bound({ segment, std::numeric_limits<cluster_id>::max() });
  dbState.createdClusters.erase(createdClustersBegin, createdClustersEnd);

  auto deletedClustersBegin = dbState.deletedClusters.lower_bound({ segment, 0 });
  auto deletedClustersEnd = dbState.deletedClusters.upper_bound({ segment, std::numeric_limits<cluster_id>::max() });
  dbState.deletedClusters.erase(deletedClustersBegin, deletedClustersEnd);


  // If we created the segment during this transaction, then we must discard this info as well
  dbState.createdSegments.erase(segment);

  // Finally note down that we deleted it (we don't construct a writtenBlob entry for SegmentDeleteId, that is handled in commit())
  dbState.deletedSegments.insert(segment);

  // The segment with all blobs is erased from the cache in the calling Database::DeleteSegment()
}



std::optional<std::pair<const void*, blob_size>> Transaction::ReadBlob(Database* database, const BlobLocation& location) const {
  assert(session->OwnsLock());
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


void Transaction::MergeBlobIdList(Database* database, segment_id segment, cluster_id cluster, std::vector<blob_id>& blobs) {
  assert(session->OwnsLock());
  if (auto dbState = state->GetDatabaseState(database->id)) {
    assert(!dbState->IsClusterDeleted(segment, cluster)); // this should already be validated by the caller before attempting to merge the list

    // Remove blobs marked for deletion
    blobs.erase(std::remove_if(blobs.begin(), blobs.end(), [=](blob_id blob) { return dbState->deletedBlobs.count(BlobLocation(segment, cluster, blob)) != 0; }), blobs.end());

    // We may need to sort the blob list after merging if we created a blob at a lower id than the already existing blobs
    bool needsSort = false;

    // Insert created blobs
    auto createdBlobsBegin = dbState->createdBlobs.lower_bound(BlobLocation(segment, cluster, 0));
    auto createdBlobsEnd = dbState->createdBlobs.upper_bound(BlobLocation(segment, cluster, std::numeric_limits<blob_id>::max()));
    for (auto pos = createdBlobsBegin; pos != createdBlobsEnd; ++pos) {
      if (!needsSort && !blobs.empty() && blobs.back() > pos->blob) {
        // We are inserting a blob with a lower id at the end (can happen due to CreateBlobAt) so we must sort
        needsSort = true;
      }

      blobs.push_back(pos->blob);
    }

    // Both deletion and insertion (usually) preserve the ascending sort order of the blob ids, so no sort necessary here
    // unless a blob has been created at a lower id with CreateBlobAt() - we only perform the sort if it is actually necessary
    if (needsSort) {
      std::sort(blobs.begin(), blobs.end());
    }
  }
}


void Transaction::MergeClusterIdList(Database* database, segment_id segment, std::vector<cluster_id>& clusters) {
  assert(session->OwnsLock());
  if (auto dbState = state->GetDatabaseState(database->id)) {
    assert(!dbState->IsSegmentDeleted(segment)); // this should already be validated by the caller before attempting to merge the list

    // Remove clusters marked for deletion
    clusters.erase(std::remove_if(clusters.begin(), clusters.end(), [=](cluster_id cluster) { return dbState->deletedClusters.count({ segment, cluster }) != 0; }), clusters.end());

    // Insert created clusters
    auto createdClustersBegin = dbState->createdClusters.lower_bound({ segment, 0 });
    auto createdClustersEnd = dbState->createdClusters.upper_bound({ segment, std::numeric_limits<cluster_id>::max() });
    for (auto pos = createdClustersBegin; pos != createdClustersEnd; ++pos) {
      clusters.push_back(pos->second);
    }

    // Both deletion and insertion preserve the ascending sort order of the cluster ids, so no sort necessary here
  }
}

void Transaction::MergeSegmentIdList(Database* database, std::vector<segment_id>& segments) {
  assert(session->OwnsLock());
  if (auto dbState = state->GetDatabaseState(database->id)) {
    // Remove segments marked for deletion
    segments.erase(std::remove_if(segments.begin(), segments.end(), [=](segment_id segment) { return dbState->deletedSegments.count(segment) != 0; }), segments.end());

    // Insert created segments
    for (auto segment : dbState->createdSegments) {
      segments.push_back(segment);
    }

    // Both deletion and insertion preserve the ascending sort order of the segment ids, so no sort necessary here
  }
}


void Transaction::TransferAndClearState(const Session::Handle& session) {
  auto& transactions = session->Transactions();

  for (auto& [connectionId, transaction] : transactions.active) {
    transaction.TransferStickyLocks();
  }

  // Reset any transaction state implicitly deleting all Transaction objects
  transactions.active.clear();
}


void Transaction::TransferStickyLocks() {
  for (auto& [databaseId, dbState] : state->forDatabase) {
    dbState.database->AssignStickyLocks(std::move(dbState.heldLocks));
  }
}



}

