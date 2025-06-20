#include "pch.hpp"
#include "include/server/Database.hpp"

namespace blobs {
namespace server {

std::map<std::string, Database, std::less<>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), snapshot(new Snapshot) {}

Database& Database::Get(std::string_view databaseName) {
  auto pos = databases.find(databaseName);
  if (pos != databases.end()) {
    return pos->second;
  } 

  std::string nameStr(databaseName.data(), databaseName.size());
  return databases.emplace(nameStr, Database(nameStr)).first->second;
}

Blob* Database::GetBlob(const BlobLocation& location) {
  return snapshot->GetBlob(location);
}

Segment* Database::GetSegment(segment_id segment) {
  return snapshot->GetSegment(segment);
}


bool Database::AcquireLocks(const network::message::BlobsRead& message) {
  auto client = message.clientId;
  bool write = (message.lockMode != network::message::BlobsRead::LockMode::Read);

  bool canAcquireLocks = std::all_of(message.begin(), message.end(), [=](const BlobLocation& location) {
    return CanClientAcquireLock(client, location, write);
  });
  
  if (canAcquireLocks) {
    // Acquire all the locks at once
    for (auto& location : message) {
      AcquireClientLock(client, location, write);
    }
  }

  FIXME("what if the blobs we are trying to lock don't exist? This should be communicated back to the caller too!");
  return canAcquireLocks;
}

bool Database::ClientOwnsWriteLock(client_id client, const BlobLocation& location) const {
  auto pos = locks.find(location);
  return (pos != locks.end()) ? pos->OwnsWriteLock(client) : false;
}



bool Database::CanClientAcquireLock(client_id client, const BlobLocation& location, bool write) {
  TODO("Also handle special locking rules: When client attempts to lock cluster's blob table, no write locks on any blob may exist in that cluster!");

  auto pos = locks.find(location);
  if (pos == locks.end()) {
    return true; // no lock yet at that location -> lock is possible
  }

  // Check the lock whether the client can acquire the specified access
  return pos->CanAcquire(client, write);
}


void Database::AcquireClientLock(client_id client, const BlobLocation& location, bool write) {
  auto pos = locks.find(location);
  if (pos == locks.end()) {
    // Simple case, nobody requested a lock for this location yet -> create a new lock held by this client
    locks.insert(Lock(location, client, write));
  } else {
    // Lock already exists
    auto& lock = const_cast<Lock&>(*pos); // const_cast is safe here, because we don't modify the ordering criterion
    lock.Acquire(client, write);
  }
}


bool Database::QueueReadCheckDeadlock(network::MessagePointer_T<network::message::BlobsRead>&& message) {
  TODO("Check for Deadlocks");
  queuedReads.push_back(std::move(message));
  return true;
}
  

void Database::AbortClientTransaction(client_id client, const std::vector<BlobLocation>& locksToRelease) {
  ReleaseLocks(client, locksToRelease);

  for (auto pos = queuedReads.begin(); pos != queuedReads.end();) {
    if ((*pos)->clientId == client) {
      // Erase this queued read message
      // Increment the iterator before erasing as it would otherwise be invalidated by the erase operation
      queuedReads.erase(pos++);
    } else {
      ++pos;
    }
  }

}


Database::CommitResult::CommitResult(Database& database, std::unique_ptr<Snapshot> snapshot) : database(database), snapshot(std::move(snapshot)) {}

commit_id Database::CommitResult::ApplyToDatabase() {
  // As inner class we can directly access the database snapshot
  database.snapshot = std::move(snapshot);
  return database.snapshot->commitId;
}

Database::CommitResult Database::CalculateCommitResult(network::MessagePointer_T<network::message::TransactionCommit>* commitPos, network::MessagePointer_T<network::message::TransactionCommit>* commitEnd) {
  // Create the new snapshot implicitly also setting the new commit id
  auto newSnapshot = std::make_unique<Snapshot>(*snapshot);

  // Now apply the commit messages one by one to the snapshot modifying it in the process
  for (auto pos = commitPos, end = commitEnd; pos != end; ++pos) {
    newSnapshot->ApplyCommitMessage(**pos);
  }
  
  return CommitResult(*this, std::move(newSnapshot));
}


void Database::ReleaseLocks(client_id client, const std::vector<BlobLocation>& locations) {
  for (auto& location : locations) {
    auto lock = locks.find(location);
    assert(lock != locks.end()); // This would indicate a programming error as the client assumes a lock which he doesn't has.
    // The const_cast here is valid as we do not modify the sorting order in any respect
    bool empty = const_cast<Lock&>(*lock).Release(client);
    // Should we delete this Lock object now or keep it in memory forever? What is worse performance wise?
  }
}



Database::Snapshot::Snapshot(commit_id commitId) : commitId(commitId), nextFreeSegmentId(1), nextFreeSegmentIdBlob(constants::NextFreeBlobId, commitId) {
  TODO("Add proper loading from database file");
  segments.emplace(0, std::make_shared<Segment>(0, commitId));
  nextFreeSegmentIdBlob.SetIdContent(nextFreeSegmentId);
}

/** Creates a copy of the snapshot with an incremented commit id
 */
Database::Snapshot::Snapshot(const Snapshot& other) : commitId(other.commitId+1), nextFreeSegmentId(other.nextFreeSegmentId), 
                                                      nextFreeSegmentIdBlob(other.nextFreeSegmentIdBlob), segments(other.segments) {}




Blob* Database::Snapshot::GetBlob(const BlobLocation& location) {
  if (location.segment == constants::NextFreeSegmentId && location.cluster == constants::NextFreeClusterId && location.blob == constants::NextFreeBlobId) {
    // Return special blob holding the next free segment id
    return &nextFreeSegmentIdBlob;
  }
  
  // Otherwise perform regular lookup
  if (auto segment = GetSegment(location.segment)) {
    // Use GetBlob() to support `NextFreeClusterId`
    return segment->GetBlob(location.cluster, location.blob);
  }
  return nullptr;
}


Segment* Database::Snapshot::GetSegment(segment_id segment) {
  // Here we cannot handle the `NextFreeSegmentId` constant, because we would need to return a whole segment
  auto pos = segments.find(segment);
  return (pos != segments.end()) ? pos->second.get() : nullptr;
}


Segment* Database::Snapshot::UpdateSegment(segment_id segment) {
  auto& segmentPtr = segments[segment];
  if (!segmentPtr) {
    // Create if not yet exist
    segmentPtr = std::make_shared<Segment>(segment, commitId);
  } else if (segmentPtr->commitId != commitId) {
    // Segment hasn't been copied yet in this transaction -> do it now
    segmentPtr = std::make_shared<Segment>(*segmentPtr, commitId);
  }

  return segmentPtr.get();
}

void Database::Snapshot::DeleteSegment(segment_id segment) {
  segments.erase(segment);
}


void Database::Snapshot::ApplyCommitMessage(network::message::TransactionCommit& commitMessage) {

  // Apply each modification in sequence
  for (auto pos = commitMessage.begin(), end = commitMessage.end(); pos != end; ++pos) {
    auto& update = *pos;


    // Validate commit message already verified that the nextFreeXXXId updates are correctly structured, but 
    // we will validate them in debug mode using assertions anyway.
    if (update.segment == constants::NextFreeSegmentId) {
      // One or more segments have been created in this transaction -> update the next free segment id
      assert(update.cluster == constants::NextFreeClusterId);
      assert(update.blob == constants::NextFreeBlobId);
      SetNextFreeSegmentId(pos.ReadId<segment_id>());
    } else if (update.cluster == constants::SegmentDeleteId) {
      // A segment is being deleted by writing to the SegmentDeleteId cluster
      assert(update.blob == constants::ClusterDeleteId);
      DeleteSegment(update.segment);
    } else {
      // Fetch a copy of the segment this update refers to and create it if necessary
      auto segment = UpdateSegment(update.segment);

      if (update.cluster == constants::NextFreeClusterId) {
        // One or more clusters are being created in this segment -> update the next free cluster id of the segment
        assert(update.blob == constants::NextFreeBlobId);
        segment->SetNextFreeClusterId(pos.ReadId<cluster_id>());
      } else if (update.blob == constants::ClusterDeleteId) {
        // A cluster is being deleted by writing to ClusterDeleteId blob
        segment->DeleteCluster(update.cluster);

      } else {
        // Fetch a copy of the cluster this update refers to and create it if necessary
        auto cluster = segment->UpdateCluster(update.cluster);


        if (update.blob == constants::NextFreeBlobId) {
          // One or more blobs are being created in this cluster -> update the next free blob id of the cluster
          cluster->SetNextFreeBlobId(pos.ReadId<blob_id>());
        } else if (update.blobSize == constants::DeleteBlobSize) {
          // The specified blob is being deleted
          cluster->DeleteBlob(update.blob);
        } else {
          // Regular blob content update
          cluster->UpdateBlob(update.blob)->SetContent(pos.ReadData());
        }
      }
    }
  }
}


void Database::Snapshot::SetNextFreeSegmentId(segment_id nextFreeId) {
  nextFreeSegmentId = nextFreeId;
  nextFreeSegmentIdBlob.SetIdContent(nextFreeSegmentId);
}


}}
