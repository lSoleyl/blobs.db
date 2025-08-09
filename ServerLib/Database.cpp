#include "pch.hpp"
#include "include/server/Database.hpp"
#include "include/server/Server.hpp"
#include "include/server/File.hpp"

namespace blobs {
namespace server {

std::map<std::string, Database, std::less<>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), snapshot(new Snapshot), fileHandle(INVALID_HANDLE_VALUE), useCount(0) {
  // In-Memory databases start with "mem:" prefix and are always loaded
  fileDatabase = !this->name._Starts_with("mem:");
  loaded = !fileDatabase;
}

Database* Database::Get(std::string_view databaseName) {
  auto pos = databases.find(databaseName);
  return (pos != databases.end()) ? &pos->second : nullptr;
}



Database& Database::Open(std::string_view databaseName, client_id clientId) {
  // Check whether database was already opened
  auto database = Get(databaseName);

  if (!database) {
    // Database wasn't opened yet -> enter new database
    std::string nameStr(databaseName.data(), databaseName.size());
    database = &databases.emplace(nameStr, Database(nameStr)).first->second;
    // The newly created database may already be fully loaded in case of an in-memory database

    // Start the loading process if necessary
    if (database->fileDatabase) {
      database->LoadFromFile();
    }
  }


  if (database->loaded) {
    // Database is already fully loaded -> call callback for client immediately
    Server::Instance().HandleDatabaseOpenResult(*database, network::message::DatabaseOpenResponse::Result::SUCCESS, clientId);
  } else {
    // Database is in the process of being loaded -> register client for callback
    database->clientsWaitingForLoading.push_back(clientId);
  }

  // One more client is using this database
  ++database->useCount;
  return *database;
}


void Database::Release() {
  if (--useCount == 0) {
    TODO("Maybe add some delay in case the database is reopened soon");
    // Last client called release -> close the database file and delete this object
    Close();
  }
}



void Database::Close() {
  TODO("If DB IO-Thread is still running -> mark the database as to-delete and return immediately performing a delayed deletion.");
  
  if (fileHandle != INVALID_HANDLE_VALUE) {
    CloseHandle(fileHandle);
    fileHandle = INVALID_HANDLE_VALUE;
  }

  // Delete this object by removing it from the databases map
  databases.erase(name);
}


void Database::LoadFromFile() {
  TODO("Maybe this should be the Database-Thread and we simply create one thread per database, which exclusively performs read/write operations on the database");
  TODO("This could become the main IO thread for the database and we could also post tasks to it via an IOCompletionPort to process if the thread is idle");
  std::thread loadThread([this]() {
    TODO("Support Unicode (UTF-8) paths later on");
    fileHandle = CreateFileA(
      name.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      0 /*exclusive access*/,
      NULL,
      // for now no FILE_FLAG_OVERLAPPED as we want to perform serial reads/writes in this thread alone
      // We don't specify FILE_FLAG_NO_BUFFERING as it requires us to always read/write whole sectors, which would only complicate things
      // We don't specify FILE_FLAG_WRITE_THROUGH because we only need to flush the writes once we perform the final pointer update (or right before and right after).
      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, 
      NULL
    );

    if (fileHandle == INVALID_HANDLE_VALUE) {
      TODO("Handle error opening database file");
      TODO("Check whether what the reason was and translate the most common ones into own error codes to return");
      Server::Instance().GetCompletionPort().PostSimpleTask([this]() {
        CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result::DATABASE_OPEN_FAILED);
        // And close the database
        Close();
      });
      // Quit the Database IO thread.
      return;
    }

    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      // Database doesn't exist yet -> initialize it
      if (!InitializeDatabaseFile()) {
        TODO("Get more precise error message to report something useful back to the client");
        Server::Instance().GetCompletionPort().PostSimpleTask([this]() {
          CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result::DATABASE_OPEN_FAILED);
          // And close the database
          Close();
        });
        // Quit the Database IO thread.
        return;
      }
    }

    // Now the database file exists -> read it and convert it into memory objects
    if (!ReadInitialFileDatabaseData()) {
      Server::Instance().GetCompletionPort().PostSimpleTask([this]() {
        CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result::DATABASE_OPEN_FAILED);
        // And close the database
        Close();
      });
      // Quit the Database IO thread.
      return;
    }

      
    TODO("Process any outstanding transaction log entries?");

    // Notify the server about the completed database load
    Server::Instance().GetCompletionPort().PostSimpleTask([this]() { CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result::SUCCESS); });
  });


  // don't wait for this thread to complete
  loadThread.detach();
}


bool Database::InitializeDatabaseFile() {
  const uint64_t INITIAL_SIZE = 1024; // The database structures ~216 Bytes, allocate the remaining space as free memory
  // Construct all the datastructures in a memory buffer to write it to file in just one write operation
  std::vector<char> fileBuffer(INITIAL_SIZE, 0); 

  uint64_t currentOffset = 0;
  auto* dbStruct = reinterpret_cast<file::Database*>(fileBuffer.data());
  dbStruct->header.Initialize();
  currentOffset += sizeof(file::Database);

  // Construct snapshot
  dbStruct->roots[0].snapshotOffset = currentOffset;
  auto* snapshot = reinterpret_cast<file::Snapshot*>(fileBuffer.data() + currentOffset);
  snapshot->size = sizeof(file::Snapshot) + sizeof(file::Snapshot::SegmentRange) + sizeof(uint64_t);
  snapshot->commitId = 1;
  snapshot->nextFreeSegmentId = 1;
  auto& segmentRange = *snapshot->begin();
  segmentRange.startId = 0;
  segmentRange.endId = 1;

  // Segment 0
  currentOffset += snapshot->size;
  *segmentRange.begin() = currentOffset;
  auto* segment = reinterpret_cast<file::Segment*>(fileBuffer.data() + currentOffset);
  segment->size = sizeof(file::Segment) + sizeof(file::Segment::ClusterRange) + sizeof(uint64_t);
  segment->commitId = 1;
  segment->nextFreeClusterId = 1;
  auto& clusterRange = *segment->begin();
  clusterRange.startId = 0;
  clusterRange.endId = 1;

  // Cluster 0
  currentOffset += segment->size;
  *clusterRange.begin() = currentOffset;
  auto* cluster = reinterpret_cast<file::Cluster*>(fileBuffer.data() + currentOffset);
  cluster->size = sizeof(file::Cluster) + sizeof(file::Cluster::BlobRange) + sizeof(uint64_t);
  cluster->commitId = 1;
  cluster->nextFreeBlobId = 1;
  auto& blobRange = *cluster->begin();
  blobRange.startId = 0;
  blobRange.endId = 1;

  // Blob 0
  currentOffset += cluster->size;
  *blobRange.begin() = currentOffset;
  auto* blob = reinterpret_cast<file::Blob*>(fileBuffer.data() + currentOffset);
  blob->size = sizeof(file::Blob); // + 0 bytes of data
  blob->commitId = 1;

  // Free list with one free block holding all the memory up to 1024 bytes
  currentOffset += blob->size;
  dbStruct->roots[0].freelistOffset = currentOffset;
  auto* freeList = reinterpret_cast<file::FreeList*>(fileBuffer.data() + currentOffset);
  freeList->size = sizeof(file::FreeList) + sizeof(file::FreeList::Block);
  freeList->endOffset = INITIAL_SIZE;
  currentOffset += freeList->size;

  auto& freeBlock = *freeList->begin();
  freeBlock.blockOffset = currentOffset;
  freeBlock.blockSize = freeList->endOffset - currentOffset;

  DWORD bytesWritten;
  if (!WriteFile(fileHandle, fileBuffer.data(), fileBuffer.size(), &bytesWritten, NULL) || bytesWritten != fileBuffer.size()) {
    // Failed to write    
    TODO("Handle creation error... maybe return the error code?");
    return false;
  }

  FlushFileBuffers(fileHandle);
  return true;
}


bool Database::ReadInitialFileDatabaseData() {
  LARGE_INTEGER offset = { 0 };
  SetFilePointerEx(fileHandle, offset, NULL, FILE_BEGIN);

  DWORD bytesRead;
  file::Database dbStruct;
  if (!ReadFile(fileHandle, &dbStruct, sizeof(dbStruct), &bytesRead, NULL) || bytesRead != sizeof(dbStruct)) {
    // Failed to read the databse header (file may not contain such a header)
    return false;
  }

  if (!dbStruct.header.IsValid()) {
    // Invalid header, this may not be a blobs.db database -> don't open it
    return false;
  }



  TODO("Read the header data and create the corresponding datastructures");
  TODO("But for that we also need to be able to store the file offsets and some load state of segment/cluster/blob");
  TODO("We need a transient version of the FreeList as this structure must be quickly accessible by the server");

  return true;
}


void Database::CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result completionCode) {
  loaded = true;
  for (auto clientId : clientsWaitingForLoading) {
    Server::Instance().HandleDatabaseOpenResult(*this, completionCode, clientId);
  }
}


Blob* Database::GetBlob(const BlobLocation& location) {
  return snapshot->GetBlob(location);
}

Segment* Database::GetSegment(segment_id segment) {
  return snapshot->GetSegment(segment);
}

segment_id Database::GetNextFreeSegmentId() const {
  return snapshot->GetNextFreeSegmentId();
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


segment_id Database::Snapshot::GetNextFreeSegmentId() const {
  return nextFreeSegmentId;
}

void Database::Snapshot::SetNextFreeSegmentId(segment_id nextFreeId) {
  nextFreeSegmentId = nextFreeId;
  nextFreeSegmentIdBlob.SetIdContent(nextFreeSegmentId);
}


}}
