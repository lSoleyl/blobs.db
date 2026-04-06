#include "pch.hpp"
#include "include/server/Database.hpp"
#include "include/server/Server.hpp"
#include "include/server/File.hpp"
#include "include/server/Client.hpp"
#include "include/server/LockUtil.hpp"
#include <common/Paths.hpp>
#include <common/Encoding.hpp>

namespace blobs {
namespace server {

std::vector<std::unique_ptr<Database>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), useCount(0), stickyLockHandler(*this) {
  // In-Memory databases start with "mem:" prefix and are always loaded
  fileDatabase = !this->name._Starts_with("mem:");
}

Database* Database::Get(std::string_view databaseName) {
  auto pos = std::find_if(databases.begin(), databases.end(), [=](const std::unique_ptr<Database>& db) { return databaseName == db->name; });
  return (pos != databases.end()) ? pos->get() : nullptr;
}


Database::OpenResultStruct::OpenResultStruct(Database* db) : db(db), result(OpenResult::SUCCESS) {}
Database::OpenResultStruct::OpenResultStruct(OpenResult result) : db(nullptr), result(result) {}

Database::OpenResultStruct::operator bool() const {
  return result == OpenResult::SUCCESS;
}

Database::OpenResultStruct Database::Open(std::string_view databaseName, OpenMode openMode) {
  // Check whether database was already opened

  std::string resolvedDbPath;
  Database* database = nullptr;
  bool memoryDatabase = databaseName._Starts_with("mem:");
  if (memoryDatabase) {
    // An in-memory database: this is the simple case, just lookup the database in our database map
    database = Get(databaseName);
  } else {
    // A file database -> resolve the path relative to the database root dir.
    if (auto resolvedNativePath = Server::Instance().GetResolvedDatabasePath(databaseName)) {
      auto pos = std::find_if(databases.begin(), databases.end(), [&](const std::unique_ptr<Database>& db) { return Paths::IsSame(*resolvedNativePath, encoding::ToUTF16(db->name)); });
      if (pos != databases.end()) {
        // Same Database file already opened -> return it
        database = pos->get();
      } else {
        // File database opened for the first time -> update the databaseName to the fully resolved path
        resolvedDbPath = encoding::ToUTF8(*resolvedNativePath);
        databaseName = resolvedDbPath;
      }

    } else {
      // Failed to resolve path (which means a path outside of the db root dir has been specified)
      return OpenResult::ILLEGAL_DATABASE_PATH;
    }
  }

  // Handle simple open mode error conditions where database creation is not possible
  if (database) {
    if (openMode == OpenMode::CreateAlways) {
      if (database->useCount > 0) {
        // Cannot re-create a database that is in active use
        return OpenResult::CANNOT_OVERWRITE_OPEN_DATABASE;
      } else {
        // Database is still known, but not in use (this cannot happen at the moment, but is possible if we introduce a delayed close)
        // To simplify the logic below, simply close the database and reopen it.
        database->Close();
        database = nullptr;
      }
    } else if (openMode == OpenMode::CreateFailIfExist) {
      // Database already exists -> fail
      return OpenResult::DATABASE_ALREADY_EXISTS;
    }
  }


  auto openResult = OpenResult::SUCCESS;
  if (!database) {
    if (memoryDatabase && openMode == OpenMode::OpenFailIfNotExist) {
      // Attempt to open a not opened memory database -> fail
      return OpenResult::DATABASE_DOES_NOT_EXIST;
    }


    // Database wasn't opened yet -> enter new database
    std::string nameStr(databaseName.data(), databaseName.size());
    database = databases.emplace_back(new Database(nameStr)).get();
    // The newly created database may already be fully loaded in case of an in-memory database

    // Load the database from file or memry
    openResult = database->fileDatabase ? database->LoadFromFile(openMode) : database->InitializeInMemory();
  }

  if (openResult != OpenResult::SUCCESS) {
    // Loading failed -> close it and send error code back to the client
    database->Close();
    return openResult;
  }
   
  // One more client is using this database
  ++database->useCount;

  // Database sucessfully opened -> return it
  return database;
}


void Database::Release() {
  if (--useCount == 0) {
    TODO("Maybe add some delay in case the database is reopened soon");
    // Last client called release -> close the database file and delete this object
    Close();
  }
}



void Database::Close() {
  file.Close();

  // Delete this object by removing it from the databases list
  auto pos = std::find_if(databases.begin(), databases.end(), [=](const std::unique_ptr<Database>& db) { return db.get() == this; });
  databases.erase(pos);
}



Database::OpenResult Database::InitializeInMemory() {
  // Create the snapshot with segment cluster and blob
  auto snapshot = std::make_unique<Snapshot>();
  auto segment = snapshot->UpdateSegment(0, nullptr);
  snapshot->SetNextFreeSegmentId(1);

  // Cluster 0 and Blob 0 are created implicitly
  this->snapshot = std::move(snapshot);
  return OpenResult::SUCCESS; // this operation can never fail
}

namespace {

DWORD convertDbOpenMode(Database::OpenMode openMode) {
  switch (openMode) {
    case Database::OpenMode::CreateIfNotExist: return OPEN_ALWAYS;
    case Database::OpenMode::OpenFailIfNotExist: return OPEN_EXISTING;
    case Database::OpenMode::CreateFailIfExist: return CREATE_NEW;
    case Database::OpenMode::CreateAlways: return CREATE_ALWAYS;
  }

  assert(false); // new unhandled open mode?
  return 0;
}

}



Database::OpenResult Database::LoadFromFile(OpenMode openMode) {
  TODO("We could create a database IO thread per database to avoid waiting for IO in the main thread. This would however complicate synchronization of operations.");

  try {
    bool emptyFile;
    bool modeSpecificError;
    file = FileBackend::OpenExclusive(name.c_str(), convertDbOpenMode(openMode), emptyFile, modeSpecificError);
    if (!file) {
      if (modeSpecificError) {
        // The following error codes are only correct if modeSpecifcError is set, because they could also fail, because
        // the file is already opened for exclusive access or because the process has insufficient permissions to open/create the file.
        if (openMode == OpenMode::CreateFailIfExist) {
          return OpenResult::DATABASE_ALREADY_EXISTS;
        } else if (openMode == OpenMode::OpenFailIfNotExist) {
          return OpenResult::DATABASE_DOES_NOT_EXIST;
        }
      }

      TODO("Check what the reason was and translate the most common ones into own error codes/messages to return");
      throw std::exception("Failed to open/create database file for exclusive writing");
    }

    if (emptyFile) {
      // Database doesn't exist yet -> initialize it
      InitializeDatabaseFile();
    }

    // Now the database file exists -> read it and convert it into memory objects
    ReadInitialFileDatabaseData();


    // Database successfully loaded
    return OpenResult::SUCCESS;

  } catch (std::exception& ex) {
    // Opening the database failed for some reason. The caller is responsible for calling Close()!
    return OpenResult::DATABASE_OPEN_FAILED;
  }
}


void Database::InitializeDatabaseFile() {
  const uint64_t INITIAL_SIZE = 1024; // The database structures ~216 Bytes, allocate the remaining space as free memory
  // Construct all the datastructures in a memory buffer to write it to file in just one write operation
  std::vector<char> fileBuffer(INITIAL_SIZE, 0); 

  auto* dbStruct = reinterpret_cast<file::Database*>(fileBuffer.data());
  dbStruct->header.Initialize();

  // Construct snapshot and store reference to it in the active root
  auto& snapshotReference = dbStruct->roots[0].snapshot;
  snapshotReference.offset = sizeof(file::Database);
  snapshotReference.size = sizeof(file::Snapshot) + sizeof(file::Snapshot::SegmentRange) + sizeof(file::BlockReference);

  auto* snapshot = reinterpret_cast<file::Snapshot*>(fileBuffer.data() + snapshotReference.offset);
  snapshot->commitId = 1;
  snapshot->nextFreeSegmentId = 1;
  auto& segmentRange = *snapshot->begin();
  segmentRange.startId = 0;
  segmentRange.endId = 1;

  // Segment 0
  auto& segmentReference = *segmentRange.begin();
  segmentReference.offset = snapshotReference.EndOffset();
  segmentReference.size = sizeof(file::Segment) + sizeof(file::Segment::ClusterRange) + sizeof(file::BlockReference);

  auto* segment = reinterpret_cast<file::Segment*>(fileBuffer.data() + segmentReference.offset);
  segment->commitId = 1;
  segment->nextFreeClusterId = 1;
  auto& clusterRange = *segment->begin();
  clusterRange.startId = 0;
  clusterRange.endId = 1;

  // Cluster 0
  auto& clusterReference = *clusterRange.begin();
  clusterReference.offset = segmentReference.EndOffset();
  clusterReference.size = sizeof(file::Cluster) + sizeof(file::Cluster::BlobRange) + sizeof(file::BlockReference);
  
  auto* cluster = reinterpret_cast<file::Cluster*>(fileBuffer.data() + clusterReference.offset);
  cluster->commitId = 1;
  cluster->nextFreeBlobId = 1;
  auto& blobRange = *cluster->begin();
  blobRange.startId = 0;
  blobRange.endId = 1;

  // Blob 0
  auto& blobReference = *blobRange.begin();
  blobReference.offset = clusterReference.EndOffset();
  blobReference.size = sizeof(file::Blob); // + 0 bytes of data

  auto* blob = reinterpret_cast<file::Blob*>(fileBuffer.data() + blobReference.offset);
  blob->commitId = 1;

  // Free list with one free block holding all the memory up to 1024 bytes
  auto& freeListReference = dbStruct->roots[0].freeList;
  freeListReference.offset = blobReference.EndOffset();
  freeListReference.size = sizeof(file::FreeList) + sizeof(file::BlockReference);

  auto* freeList = reinterpret_cast<file::FreeList*>(fileBuffer.data() + freeListReference.offset);
  freeList->endOffset = INITIAL_SIZE;
  freeList->entryCount = 1;

  auto& freeBlock = *freeList->begin();
  freeBlock.offset = freeListReference.EndOffset();
  freeBlock.size = freeList->endOffset - freeListReference.EndOffset();

  if (!file.WriteFromMemory(fileBuffer.data(), fileBuffer.size())) {
    // Failed to write    
    throw std::exception("Failed to write initial database structure into the database file");
  }

  file.FlushFileBuffers();
  std::memcpy(&fileHeader, dbStruct, sizeof(fileHeader));
}



void Database::ReadInitialFileDatabaseData() {
  file.SetFilePosition(0);

  if (!file.ReadStruct(fileHeader)) {
    // Failed to read the databse header (file may not contain such a header)
    throw std::exception("Failed to load root database structure from database file");
  }

  if (!fileHeader.header.IsValid()) {
    // Invalid header, this may not be a blobs.db database -> don't open it
    throw std::exception("Database header is invalid");
  }


  auto& root = fileHeader.roots[fileHeader.rootIndex];
  auto fileSnapshot = file.LoadMemoryBlock<file::Snapshot>(root.snapshot);
  if (!fileSnapshot) {
    throw std::exception("Failed to load snapshot reference");
  }
  

  auto snapshot = std::make_unique<Snapshot>(fileSnapshot->commitId);
  snapshot->fileLocation = root.snapshot;
  snapshot->SetNextFreeSegmentId(fileSnapshot->nextFreeSegmentId);

  // A file snapshot holds a list of segment reference ranges (for more compact storage)
  // Load the segment map, but don't load the segments themselves yet
  for (auto it = fileSnapshot->begin(), end = fileSnapshot->end(snapshot->fileLocation.size); it != end; ++it) {
    auto& range = *it;
    auto segmentId = range.startId;
    for (auto& segmentReference : range) {
      snapshot->DelayLoadSegment(segmentId, segmentReference);
      ++segmentId;
    }
  }

  auto fileFreeList = file.LoadMemoryBlock<file::FreeList>(root.freeList);
  if (!fileFreeList) {
    throw std::exception("Failed to load free list reference");
  }
  auto freeList = std::make_unique<FreeList>(fileFreeList->endOffset);
  freeList->fileLocation = root.freeList;
  
  // Simply enter all block references as free memory blocks into the free list
  for (auto& blockReference : *fileFreeList) {
    freeList->FreeMemoryBlock(blockReference);
  }

  // We assume the free list to already be reorganized before writing it to file, so we won't do it here


  // Finally assign the loaded snapshot and free list to this database
  this->snapshot = std::move(snapshot);
  this->freeList = std::move(freeList);
}


Blob* Database::GetLoadedBlob(const BlobLocation& location) {
  return snapshot->GetLoadedBlob(location, file);
}

Cluster* Database::GetLoadedCluster(segment_id segment, cluster_id cluster) {
  return snapshot->GetLoadedCluster(segment, cluster, file);
}


Segment* Database::GetLoadedSegment(segment_id segment) {
  return snapshot->GetLoadedSegment(segment, file);
}

segment_id Database::GetNextFreeSegmentId() const {
  return snapshot->GetNextFreeSegmentId();
}

commit_id Database::GetCommitId() const {
  return snapshot->commitId;
}

Database::iterator Database::begin() {
  return snapshot->begin();
}

Database::iterator Database::end() {
  return snapshot->end();
}


bool Database::AcquireLocks(const network::message::BlobsRead& message) {
  auto client = message.clientId;
  bool write = message.NeedsWriteLock();

  
  if (!AllLocksInMessage(*this, message, [=](const BlobLocation& location) { return CanClientAcquireLock(client, location, write); })) {
    // At least one lock cannot be acquired
    return false;
  }
  
  
  // Acquire all the locks at once
  ForEachLockInMessage(*this, message, [=](const BlobLocation& location) { AcquireClientLock(client, location, write); });
  return true;
}


void Database::AcquireImplicitWriteLocks(client_id client, const std::vector<BlobLocation>& writeLocks) {
  for (auto& location : writeLocks) {
    AcquireClientLock(client, location, true);
  }
}


bool Database::ClientOwnsWriteLock(client_id client, const BlobLocation& location) const {
  auto pos = locks.find(location);
  return (pos != locks.end()) ? pos->OwnsWriteLock(client) : false;
}


bool Database::CanClientAcquireLock(client_id client, const BlobLocation& location, bool write) {
  auto pos = locks.find(location);
  if (pos == locks.end()) {
    return true; // no lock yet at that location -> lock is possible
  }

  // Check the lock whether the client can acquire the specified access
  return pos->CanAcquire(client, write, stickyLockHandler);
}


void Database::AcquireClientLock(client_id client, const BlobLocation& location, bool write) {
  auto pos = locks.find(location);
  if (pos == locks.end()) {
    // Simple case, nobody requested a lock for this location yet -> create a new lock held by this client
    locks.insert(Lock(location, client, write));
  } else {
    // Lock already exists
    auto& lock = const_cast<Lock&>(*pos); // const_cast is safe here, because we don't modify the ordering criterion
    lock.Acquire(client, write, stickyLockHandler);
  }
}



std::optional<Database::DeadlockInfo> Database::QueueReadCheckDeadlock(network::MessagePointer_T<network::message::BlobsRead>&& message) {
  // Collect all clients, whose locks conflict with this read message
  auto conflicts = CollectLockConflicts(*message);
  assert(!conflicts.empty()); // Why would we queue a read if there are no conflicts!?

  // Now check the messages of all conflicting clients for conflicts with this message's client
  auto client = message->clientId;
  bool writeLock = message->NeedsWriteLock();

  for (auto& queuedMessage : queuedReads) {
    // Only check the messages of clients, which are preventing the current message's lock acquisition
    auto lockConflict = std::find_if(conflicts.begin(), conflicts.end(), [&](const LockConflict& conflict) { return conflict.blockedBy == queuedMessage->clientId; });
    if (lockConflict != conflicts.end()) {
      if (auto conflictLocation = FindLockConflictWith(*queuedMessage, client)) {
        // We have a bidirectional locking conflict (i.e. a deadlock!)
        
        // We still push the read into the read queue to have the flexibility of not always aborting the transaction of the client sending the last message.
        queuedReads.push_back(std::move(message));
        
        // Now return an object containing the main deadlock information
        TODO("Currently DeadlockInfo does not hold the information what kind of lock the conflicting client holds, this could be useful for error reporting/troubleshooting");
        DeadlockInfo deadlock;
        deadlock.requests[0].location = lockConflict->location;
        deadlock.requests[0].client = client;
        deadlock.requests[0].writeLock = writeLock;

        deadlock.requests[1].location = *conflictLocation;
        deadlock.requests[1].client = queuedMessage->clientId;
        deadlock.requests[1].writeLock = queuedMessage->NeedsWriteLock();
        return deadlock;
      }
    }
  }
  
  // No deadlocks, simply queue the message and return an empty optional to indicate no deadlock
  queuedReads.push_back(std::move(message));
  return std::nullopt;
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


Database::CommitResult::CommitResult(Database& database, std::unique_ptr<Snapshot> snapshot, std::unique_ptr<FreeList> freeList, std::unique_ptr<MemoryBlockDelta> delta, Deleted&& deleted)
  : database(database), snapshot(std::move(snapshot)), freeList(std::move(freeList)), delta(std::move(delta)), deleted(std::move(deleted))
{}

commit_id Database::CommitResult::ApplyToDatabase() {
  
  
  if (delta) { 
    // Delta is only specified for file databases
    assert(database.fileDatabase);
    assert(database.file);

    std::vector<char> writeBuffer;
    // First we write all newly allocated memory blocks into the file
    for (auto memoryBlock : delta->GetAllocated()) {
      if (!database.file.StoreMemoryBlock(*memoryBlock, writeBuffer)) {
        TODO("Handle file write errors somehow? This should abort the transaction commit");
        assert(false);
      }
    }

    // Flush all writes before the database header to disk
    database.file.FlushFileBuffers();
    
    // Now update the database header itself
    database.file.SetFilePosition(0);
    database.fileHeader.rootIndex = database.fileHeader.rootIndex ^ 1; // flip the 1 bit value
    auto& root = database.fileHeader.roots[database.fileHeader.rootIndex];
    root.freeList = freeList->fileLocation;
    root.snapshot = snapshot->fileLocation;


    if (!database.file.WriteStruct(database.fileHeader)) {
      TODO("Handle file write errors by aborting the transaction commit");
      assert(false);
      // Rollback the change to the datbase root
      database.fileHeader.rootIndex = database.fileHeader.rootIndex ^ 1; // flip the 1 bit value
    }

    // And flush the file buffers BEFORE updating the transient state of the database
    database.file.FlushFileBuffers();
  }

  // As inner class we can directly access the database snapshot
  database.snapshot = std::move(snapshot);
  database.freeList = std::move(freeList);
  return database.snapshot->commitId;
}

Database::CommitResult Database::CalculateCommitResult(network::MessagePointer_T<network::message::TransactionCommit>* commitPos, network::MessagePointer_T<network::message::TransactionCommit>* commitEnd) {
  // Create the new snapshot implicitly also setting the new commit id
  auto newSnapshot = std::make_unique<Snapshot>(*snapshot);
  auto newFreeList = freeList ? std::make_unique<FreeList>(*freeList) : nullptr;
  auto delta = freeList ? std::make_unique<MemoryBlockDelta>() : nullptr;
  Deleted deleted;

  if (delta) {
    // Mark the new snapshot as allocated and the current one as released
    delta->Allocated(newSnapshot.get());
    delta->Released(snapshot.get());
  }

  // Now apply the commit messages one by one to the snapshot modifying it in the process
  for (auto pos = commitPos, end = commitEnd; pos != end; ++pos) {
    newSnapshot->ApplyCommitMessage(**pos, delta.get(), deleted);
  }

  if (newFreeList) {
    newFreeList->AllocateAndApplyDelta(*delta);
  }
  
  return CommitResult(*this, std::move(newSnapshot), std::move(newFreeList), std::move(delta), std::move(deleted));
}

void Database::ReleaseLock(client_id client, const BlobLocation& location) {
  auto lock = locks.find(location);
  assert(lock != locks.end()); // This would indicate a programming error as the client assumes a lock which he doesn't have.
  // The const_cast here is valid as we do not modify the sorting order in any respect
  bool empty = const_cast<Lock&>(*lock).Release(client);

  if (empty) {
    // Lock no longer held by any client -> remove it from the locks data structure to free up memory.
    // The use of sticky locks should actually avoid the frequent deletion and re-creation of commonly used locks.
    // Deleting the lock object is important to release the lock structures for deleted blobs, clusters, segments as they
    // cannot ever be re-acquired and keeping them around would only slow down every following lookup.
    locks.erase(lock);
  }
}


void Database::ReleaseLocks(client_id client, const std::vector<BlobLocation>& locations) {
  for (auto& location : locations) {
    ReleaseLock(client, location);
  }
}

Database::LockConflict::LockConflict(const BlobLocation& location, client_id blockedBy) : location(location), blockedBy(blockedBy) {}


std::vector<Database::LockConflict> Database::CollectLockConflicts(const network::message::BlobsRead& message) {
  std::vector<Database::LockConflict> conflicts;
  auto client = message.clientId;
  bool write = message.NeedsWriteLock();

  ForEachLockInMessage(*this, message, [&](const BlobLocation& location) {
    auto lock = locks.find(location);
    if (lock != locks.end()) {
      for (auto conflictingClient : lock->CollectConflictingClients(client, write, stickyLockHandler)) {
        conflicts.push_back(LockConflict(location, conflictingClient));
      }
    }
  });

  return conflicts;
}



std::optional<BlobLocation> Database::FindLockConflictWith(const network::message::BlobsRead& message, client_id conflictingClient) {
  auto client = message.clientId;
  bool write = message.NeedsWriteLock();


  std::optional<BlobLocation> conflictLocation;

  AllLocksInMessage(*this, message, [&](const BlobLocation& location) {
    auto lock = locks.find(location);
    if (lock != locks.end()) {
      auto conflicts = lock->CollectConflictingClients(client, write, stickyLockHandler);
      if (std::find(conflicts.begin(), conflicts.end(), conflictingClient) != conflicts.end()) {
        // This location conflicts with the given conflicting client -> return it as a signal that we found the lock conflict we were looking for
        conflictLocation = location;
        return false; // abort iteration we found our conflict
      }
    }

    return true; // continue iteration
  });

  // Return the conflict location if any has been found
  return conflictLocation;
}




Database::StickyLockHandler::StickyLockHandler(const Database& db) : db(db) {}

bool Database::StickyLockHandler::CanRevokeStickyLock(client_id id) const {
  auto& client = Client::Get(id);
  return !client.IsInsideTransaction();
}

void Database::StickyLockHandler::RevokeStickyLock(client_id id, const BlobLocation& location) {
  auto& client = Client::Get(id);
  auto dbId = client.LookupDatabase(db);

  // Programming error if we try to revoke a database lock for a client, which didn't open this database... 
  // could also be caused by not properly cleaning up the held locks after the client closes the database/connection
  assert(dbId); 
  client.RevokeStickyLock(*dbId, location);
}





Database::Snapshot::Snapshot(commit_id commitId) : commitId(commitId), nextFreeSegmentId(0), nextFreeSegmentIdBlob(constants::NextFreeBlobId, commitId) {
  nextFreeSegmentIdBlob.SetIdContent(nextFreeSegmentId);
}

/** Creates a copy of the snapshot with an incremented commit id
 */
Database::Snapshot::Snapshot(const Snapshot& other) : commitId(other.commitId+1), nextFreeSegmentId(other.nextFreeSegmentId), 
                                                      nextFreeSegmentIdBlob(other.nextFreeSegmentIdBlob), segments(other.segments) {}




Blob* Database::Snapshot::GetLoadedBlob(const BlobLocation& location, const FileBackend& file) {
  if (location.segment == constants::NextFreeSegmentId && location.cluster == constants::NextFreeClusterId && location.blob == constants::NextFreeBlobId) {
    // Return special blob holding the next free segment id
    return &nextFreeSegmentIdBlob;
  }
  
  // Otherwise perform regular lookup
  if (auto segment = GetLoadedSegment(location.segment, file)) {
    // Use GetLoadedBlob() to support `NextFreeClusterId`
    return segment->GetLoadedBlob(location.cluster, location.blob, file);
  }
  return nullptr;
}


Cluster* Database::Snapshot::GetLoadedCluster(segment_id segmentId, cluster_id clusterId, const FileBackend& file) {
  if (auto segment = GetLoadedSegment(segmentId, file)) {
    return segment->GetLoadedCluster(clusterId, file);
  }

  return nullptr;
}


Segment* Database::Snapshot::GetLoadedSegment(segment_id segmentId, const FileBackend& file) {
  if (auto segment = GetSegment(segmentId)) {
    // Load the segment from file if it isn't loaded yet
    TODO("Once we use async IO to load stuff, we must handle LOADED and LOADING separately");
    if (segment->status != Status::LOADED) {
      segment->LoadFrom(file);
      assert(segment->status == Status::LOADED);
    }

    return segment;
  }

  return nullptr;
}


Segment* Database::Snapshot::GetSegment(segment_id segment) {
  // Here we cannot handle the `NextFreeSegmentId` constant, because we would need to return a whole segment
  auto pos = segments.find(segment);
  return (pos != segments.end()) ? pos->second.get() : nullptr;
}


Segment* Database::Snapshot::UpdateSegment(segment_id segment, MemoryBlockDelta* delta) {
  TODO("What if the segment hasn't been loaded from the database yet?");

  auto& segmentPtr = segments[segment];
  if (!segmentPtr) {
    // Create if not yet exist
    segmentPtr = std::make_shared<Segment>(segment, commitId);

    // And implicitly create cluster 0
    segmentPtr->UpdateCluster(0, delta);
    segmentPtr->SetNextFreeClusterId(1);

    if (delta) {
      delta->Allocated(segmentPtr.get());
    }
  } else if (segmentPtr->commitId != commitId) {
    // Segment hasn't been copied yet in this transaction -> do it now
    if (delta) {
      delta->Released(segmentPtr.get());
    }
    segmentPtr = std::make_shared<Segment>(*segmentPtr, commitId);
    if (delta) {
      delta->Allocated(segmentPtr.get());
    }
  }

  return segmentPtr.get();
}


void Database::Snapshot::DelayLoadSegment(segment_id segment, file::BlockReference& fileReference) {
  auto& segmentPtr = segments[segment];
  
  // This operation is not allowed for already existing/loaded segments
  assert(!segmentPtr);

  segmentPtr = std::make_shared<Segment>(segment, std::numeric_limits<commit_id>::max() /* the commit id is stored inside the segment's memory block, so we cannot know it yet */);
  segmentPtr->status = MemoryBlock::Status::NOT_LOADED;
  segmentPtr->fileLocation = fileReference;
}

void Database::Snapshot::DeleteSegment(segment_id segment, MemoryBlockDelta* delta) {
  auto pos = segments.find(segment);
  if (pos != segments.end()) {
    if (delta) {
      delta->Released(pos->second.get());
      // Also make sure to release all clusters in that segment
      pos->second->ReleaseAllClusters(delta);
    }

    segments.erase(pos);
  }
}


void Database::Snapshot::ApplyCommitMessage(network::message::TransactionCommit& commitMessage, MemoryBlockDelta* delta, Deleted& deleted) {
  TODO("Maybe we should use if(constexpr) for the db-less path");


  TODO("When deleting a blob,cluster,segment we must load them full from file IFF there exists an active MVCC snapshot of this database");

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
      DeleteSegment(update.segment, delta);
      deleted.segments.push_back(update.segment);
    } else {
      // Fetch a copy of the segment this update refers to and create it if necessary
      auto segment = UpdateSegment(update.segment, delta);

      if (update.cluster == constants::NextFreeClusterId) {
        // One or more clusters are being created in this segment -> update the next free cluster id of the segment
        assert(update.blob == constants::NextFreeBlobId);
        segment->SetNextFreeClusterId(pos.ReadId<cluster_id>());
      } else if (update.blob == constants::ClusterDeleteId) {
        // A cluster is being deleted by writing to ClusterDeleteId blob
        segment->DeleteCluster(update.cluster, delta);
        deleted.clusters.push_back({ update.segment, update.cluster });

      } else {
        // Fetch a copy of the cluster this update refers to and create it if necessary
        auto cluster = segment->UpdateCluster(update.cluster, delta);

        if (update.blob == constants::NextFreeBlobId) {
          // One or more blobs are being created in this cluster -> update the next free blob id of the cluster
          cluster->SetNextFreeBlobId(pos.ReadId<blob_id>());
        } else if (update.blobSize == constants::DeleteBlobSize) {
          // The specified blob is being deleted
          cluster->DeleteBlob(update.blob, delta);
          deleted.blobs.push_back(update);
        } else {
          // Regular blob content update
          cluster->UpdateBlob(update.blob, delta)->SetContent(pos.ReadData());
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


uint64_t Database::Snapshot::CalculateRequiredSize() const {
  uint64_t size = sizeof(file::Snapshot);

  // To calculate the required file size, we need to determine the number of contiguous segment ids
  // This calculation is the reason why we opted for the sorted flat map instead of any other map as 
  // the sorted flat map makes this calculation easiest while still providing a good lookup performance.
  int contiguousBlocks = 0;
  std::optional<segment_id> lastSegmentId;
  for (auto& [segmentId, segment] : segments) {
    if (!lastSegmentId || segmentId != *lastSegmentId + 1) {
      // first block, or any following block
      ++contiguousBlocks;
    }
    lastSegmentId = segmentId;
  }

  // We have to allocate one SegmentRange for each continguous range of segment ids
  size += sizeof(file::Snapshot::SegmentRange) * contiguousBlocks;

  // And then allocate one block reference for each cluster
  size += sizeof(file::BlockReference) * segments.size();

  return size;
}



void Database::Snapshot::SerializeIntoBuffer(std::vector<char>& targetBuffer) const {
  // Assume the memory block has been correctly sized before calling this method
  assert(CalculateRequiredSize() == fileLocation.size);
  targetBuffer.resize(fileLocation.size);

  auto fileSnapshot = reinterpret_cast<file::Snapshot*>(targetBuffer.data());
  fileSnapshot->commitId = commitId;
  fileSnapshot->nextFreeSegmentId = nextFreeSegmentId;


  auto rangePos = fileSnapshot->begin();

  auto segmentsPos = segments.begin();
  auto segmentsEnd = segments.end();
  while (segmentsPos != segmentsEnd) {
    // Find the next group of contiguous segments
    auto startId = segmentsPos->first;
    auto endId = startId;

    auto writePos = rangePos->begin();
    *writePos++ = segmentsPos->second->fileLocation; // store the block reference

    // Find the end of the contiguous segments
    while (++segmentsPos != segmentsEnd && segmentsPos->first == endId + 1) {
      ++endId;
      *writePos++ = segmentsPos->second->fileLocation; // store the block reference
    }

    // Set the range of contiguous segments and advance the ranges iterator
    rangePos->startId = startId;
    rangePos->endId = endId + 1;
    ++rangePos;
  }

  // After writing the last segment range, we should end up at the end position
  assert(rangePos == fileSnapshot->end(fileLocation.size));
}


Database::Snapshot::iterator Database::Snapshot::begin() {
  return segments.begin();
}

Database::Snapshot::iterator Database::Snapshot::end() {
  return segments.end();
}


Database::FreeList::FreeList(uint64_t endOffset) : endOffset(endOffset) {}

void Database::FreeList::FreeMemoryBlock(file::BlockReference location) {
  freeList.push_back(location);
}

uint64_t Database::FreeList::AllocateMemoryBlock(uint64_t size) {
  TODO("Maybe we should search for the best fitting block to avoid database fragmentation");
  auto pos = std::find_if(freeList.begin(), freeList.end(), [size](const file::BlockReference& block) { return block.size >= size; });

  if (pos != freeList.end()) {
    // Found large enough block (allocate at the beginning of that block)
    auto offset = pos->offset;
    pos->offset += size;
    pos->size -= size;
    return offset;
  } else {
    // Grow the databse file by the required size (simply by the size of the requested block size)
    auto offset = endOffset;
    endOffset += size;
    return offset;
  }
}


void Database::FreeList::Reorganize() {
  if (freeList.size() > 1) {
    // First sort by address
    std::sort(freeList.begin(), freeList.end(), [](auto& a, auto& b) { return a.offset < b.offset; });
  
    // Merge neigbouring blocks
    auto pos = freeList.begin();
    auto next = pos + 1;
    auto end = freeList.end();

    while (next != end) {
      if (pos->EndOffset() == next->offset) {
        // Adjacent blocks -> merge into the latter one
        next->offset -= pos->size;
        next->size += pos->size;
        pos->size = 0;
      }

      ++pos;
      ++next;
    }

    // Now remove all 0 size blocks
    freeList.erase(std::remove_if(freeList.begin(), freeList.end(), [](auto& block) { return block.size == 0; }), freeList.end());
  }
}


uint64_t Database::FreeList::CalculateRequiredSize() const {
  return sizeof(file::FreeList) + sizeof(file::BlockReference) * freeList.size();
}

void Database::FreeList::SerializeIntoBuffer(std::vector<char>& targetBuffer) const {
  // Assume the memory block has been correctly sized before calling this method
  // It can be smaller than the allocated size due to reorganization
  assert(CalculateRequiredSize() <= fileLocation.size);
  targetBuffer.resize(fileLocation.size);

  auto fileFreeList = reinterpret_cast<file::FreeList*>(targetBuffer.data());
  fileFreeList->endOffset = endOffset;
  fileFreeList->entryCount = freeList.size();
  std::copy(freeList.begin(), freeList.end(), fileFreeList->begin());
}



auto Database::FreeList::begin() -> iterator {
  return freeList.begin();
}

auto Database::FreeList::end() -> iterator {
  return freeList.end();
}

size_t Database::FreeList::Size() const {
  return freeList.size();
}

void Database::FreeList::AllocateAndApplyDelta(MemoryBlockDelta& delta) {
  // This method should be called on a free list copy, which knows the file location of the previous free list
  assert(fileLocation.offset && fileLocation.size > 0);
  // Remember the original block to mark it as free memory block in the end
  auto previousListBlock = fileLocation;

  // First allocate all the memory blocks marked for allocation and store the 
  // new file locations inside the blocks
  for (auto block : delta.GetAllocated()) {
    block->fileLocation.size = block->CalculateRequiredSize();
    block->fileLocation.offset = AllocateMemoryBlock(block->fileLocation.size);
  }

  // Then estimate the maximum size of the memory block needed to hold the full free list (may get smaller due to reorganization)
  fileLocation.size = EstimateRequiredSize(delta.GetReleased().size() + 1); // +1 because we release this free list's old memory block too
  fileLocation.offset = AllocateMemoryBlock(fileLocation.size);

  // Mark the free list's memory block as allocated now to not forget to write it back into the database file
  // We cannot do this before as it would mess up the free list update
  delta.Allocated(this);

  // Only now that we performed all allocations we can safely release all memory blocks, which were released during this commit
  // This is important to be done last as we DO NOT want to accidentially allocate from a memory block, which was released during the same
  // commit as this break the copy-on-write semantics for this database and lead to unrecoverable database corruptions.
  for (auto block : delta.GetReleased()) {
    FreeMemoryBlock(block->fileLocation);
  }

  // And don't forget to release the memory block in which the previous version of this free list was allocated
  FreeMemoryBlock(previousListBlock);

  // Finally reorganize the free list entries
  Reorganize();
}



uint64_t Database::FreeList::EstimateRequiredSize(uint64_t additionalFreeBlocks) const {
  // Maximum size not taking into account that Blocks may be deleted inside Reorganize()
  auto estimatedSize = CalculateRequiredSize() + sizeof(file::BlockReference) * additionalFreeBlocks;

  // If the current free list has no free memory, then it will have to allocate one new block from the end of the file
  // which will be exactly the required size, so no new block reference will be added to the free list as it will be immediately consumed.
  return estimatedSize;
}


class DatabaseDocTestAccess {
public:
  static Database::FreeList NewFreeList(uint64_t endOffset = 0) { return Database::FreeList(endOffset); }
};


SCENARIO("FreeList Invariants") {
  GIVEN("An empty FreeList") {
    auto freeList = DatabaseDocTestAccess::NewFreeList();

    THEN("Size() should return 0") {
      REQUIRE(freeList.Size() == 0);
    }

    THEN("Allocating 100 bytes should return offset 0") {
      REQUIRE(freeList.AllocateMemoryBlock(100) == 0);
    }

    THEN("Allocating two blocks of 100 bytes should return offset 100") {
      REQUIRE(freeList.AllocateMemoryBlock(100) == 0);
      REQUIRE(freeList.AllocateMemoryBlock(100) == 100);
    }
    
    THEN("Allocating three blocks of 100 bytes should return offset 200") {
      REQUIRE(freeList.AllocateMemoryBlock(100) == 0);
      REQUIRE(freeList.AllocateMemoryBlock(100) == 100);
      REQUIRE(freeList.AllocateMemoryBlock(100) == 200);
    }

    THEN("Feering a memory block should change the size to 1") {
      freeList.FreeMemoryBlock(file::BlockReference(123, 123));
      REQUIRE(freeList.Size() == 1);
    }

    THEN("Freeing a 0 size block should also increase the size by 1") {
      freeList.FreeMemoryBlock(file::BlockReference(123, 0));
      REQUIRE(freeList.Size() == 1);
    }
  }


  GIVEN("A free list with blocks (0,100), (100,100), (200,100)") {
    auto freeList = DatabaseDocTestAccess::NewFreeList(300);
    freeList.FreeMemoryBlock(file::BlockReference(0, 100));
    freeList.FreeMemoryBlock(file::BlockReference(100, 100));
    freeList.FreeMemoryBlock(file::BlockReference(200, 100));


    THEN("Size() should return 3") {
      REQUIRE(freeList.Size() == 3);
    }

    THEN("Allocate 100 should return 0") {
      REQUIRE(freeList.AllocateMemoryBlock(100) == 0);
    }

    THEN("Allocating three blocks of 50 size should return 0,50,100") {
      REQUIRE(freeList.AllocateMemoryBlock(50) == 0);
      REQUIRE(freeList.AllocateMemoryBlock(50) == 50);
      REQUIRE(freeList.AllocateMemoryBlock(50) == 100);
    }

    THEN("Reorganize() should be able to collapse all blocks into one block of size 300") {
      freeList.Reorganize();
      REQUIRE(freeList.Size() == 1);
      auto& block = *freeList.begin();
      REQUIRE(block.offset == 0);
      REQUIRE(block.size == 300);
    }

    THEN("Afer allocating 3 blocks of 100 Reorganize() should remove all blocks") {
      freeList.AllocateMemoryBlock(100);
      freeList.AllocateMemoryBlock(100);
      freeList.AllocateMemoryBlock(100);
      freeList.Reorganize();

      REQUIRE(freeList.Size() == 0);
    }
  }

  GIVEN("A free list with blocks (0,100), (300, 50), (200,100)") {
    auto freeList = DatabaseDocTestAccess::NewFreeList(350);
    freeList.FreeMemoryBlock(file::BlockReference(0, 100));
    freeList.FreeMemoryBlock(file::BlockReference(300, 50));
    freeList.FreeMemoryBlock(file::BlockReference(200, 100));

    THEN("Reorganize() should result in two blocks") {
      freeList.Reorganize();
      REQUIRE(freeList.Size() == 2);
      auto& first = *freeList.begin();
      REQUIRE(first.offset == 0);
      REQUIRE(first.size == 100);

      auto& second = *(freeList.begin() + 1);
      REQUIRE(second.offset == 200);
      REQUIRE(second.size == 150);
    }

    THEN("Allocating 3 blocks of 100 should return 0, 200, 350") {
      REQUIRE(freeList.AllocateMemoryBlock(100) == 0);
      REQUIRE(freeList.AllocateMemoryBlock(100) == 200);
      REQUIRE(freeList.AllocateMemoryBlock(100) == 350); // must allocate AFTER the 50 block
    }

    THEN("Allocating 3 blocks of 50 should return 0, 50, 300") {
      REQUIRE(freeList.AllocateMemoryBlock(50) == 0);
      REQUIRE(freeList.AllocateMemoryBlock(50) == 50);
      REQUIRE(freeList.AllocateMemoryBlock(50) == 300);
    }
  }
}


}}
