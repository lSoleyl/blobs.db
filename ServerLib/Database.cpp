#include "pch.hpp"
#include "include/server/Database.hpp"
#include "include/server/Server.hpp"
#include "include/server/File.hpp"

namespace blobs {
namespace server {

std::map<std::string, Database, std::less<>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), fileHandle(INVALID_HANDLE_VALUE), useCount(0), loaded(false) {
  // In-Memory databases start with "mem:" prefix and are always loaded
  fileDatabase = !this->name._Starts_with("mem:");
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
    } else {
      database->InitializeInMemory();
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



void Database::InitializeInMemory() {
  // Create the snapshot with segment cluster and blob
  auto snapshot = std::make_unique<Snapshot>();
  auto segment = snapshot->UpdateSegment(0);
  snapshot->SetNextFreeSegmentId(1);

  auto cluster = segment->UpdateCluster(0);
  segment->SetNextFreeClusterId(1);

  auto blob = cluster->UpdateBlob(0);
  cluster->SetNextFreeBlobId(1);

  this->snapshot = std::move(snapshot);
  loaded = true;
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

    try {
      if (fileHandle == INVALID_HANDLE_VALUE) {
        TODO("Check what the reason was and translate the most common ones into own error codes to return");
        throw std::exception("Failed to open/create database file");
      }

      if (GetLastError() != ERROR_ALREADY_EXISTS) {
        // Database doesn't exist yet -> initialize it
        InitializeDatabaseFile();
      }

      // Now the database file exists -> read it and convert it into memory objects
      ReadInitialFileDatabaseData();

      
      TODO("Process any outstanding transaction log entries?");

      // Notify the server about the completed database load
      Server::Instance().GetCompletionPort().PostSimpleTask([this]() { CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result::SUCCESS); });



      TODO("Should we now loop and wait for our own completion port to support running tasks in this database thread?");
      TODO("Or should we simply have a queue of load/write requests to process?");

    } catch (std::exception& ex) {
      // Opening the database failed for some reason
      Server::Instance().GetCompletionPort().PostSimpleTask([this]() {
        CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result::DATABASE_OPEN_FAILED);
        // And close the database
        Close();
      });
    }
  });


  // don't wait for this thread to complete
  loadThread.detach();
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

  auto& freeBlock = *freeList->begin();
  freeBlock.offset = freeListReference.EndOffset();
  freeBlock.size = freeList->endOffset - freeListReference.EndOffset();

  DWORD bytesWritten;
  if (!WriteFile(fileHandle, fileBuffer.data(), fileBuffer.size(), &bytesWritten, NULL) || bytesWritten != fileBuffer.size()) {
    // Failed to write    
    throw std::exception("Failed to write initial database structure into the database file");
  }

  FlushFileBuffers(fileHandle);
}

namespace {

/** Returns true if the specified buffer could successfully be filled with the requested amount of bytes from the file
 *  This function will perform multiple reads if necessary
 */
bool ReadIntoMemory(HANDLE fileHandle, char* buffer, size_t bytes) {
  DWORD bytesRead;
  while (bytes > 0) {
    if (!ReadFile(fileHandle, buffer, bytes, &bytesRead, NULL)) {
      // Read failed
      return false;
    }

    bytes -= bytesRead;
    buffer += bytesRead;
  }
}



template<typename T>
bool LoadStructFromFile(HANDLE fileHandle, T& targetStruct) {
  return ReadIntoMemory(fileHandle, reinterpret_cast<char*>(&targetStruct), sizeof(T));
}



template<typename T>
std::unique_ptr<T> LoadFileReference(HANDLE fileHandle, const file::BlockReference& reference) {
  std::unique_ptr<char[]> buffer(new char[reference.size]);
  LARGE_INTEGER offset;
  offset.QuadPart = reference.offset;
  SetFilePointerEx(fileHandle, offset, NULL, FILE_BEGIN);
  
  if (ReadIntoMemory(fileHandle, buffer.get(), reference.size)) {
    return std::unique_ptr<T>(reinterpret_cast<T*>(buffer.release()));
  } else {
    // Otherwise we failed to read the specified amount of bytes -> return a nullptr to indicate that error
    return std::unique_ptr<T>();
  }
}


}




void Database::ReadInitialFileDatabaseData() {
  LARGE_INTEGER offset = { 0 };
  SetFilePointerEx(fileHandle, offset, NULL, FILE_BEGIN);

  file::Database dbStruct;
  if (!LoadStructFromFile(fileHandle, dbStruct)) {
    // Failed to read the databse header (file may not contain such a header)
    throw std::exception("Failed to load root database structure from database file");
  }

  if (!dbStruct.header.IsValid()) {
    // Invalid header, this may not be a blobs.db database -> don't open it
    throw std::exception("Database header is invalid");
  }


  auto& root = dbStruct.roots[dbStruct.rootIndex];
  auto fileSnapshot = LoadFileReference<file::Snapshot>(fileHandle, root.snapshot);
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
      auto segment = snapshot->UpdateSegment(segmentId);
      segment->fileLocation = segmentReference;
      segment->status = MemoryBlock::Status::NOT_LOADED;
      ++segmentId;
    }
  }

  auto fileFreeList = LoadFileReference<file::FreeList>(fileHandle, root.freeList);
  auto freeList = std::make_unique<FreeList>(fileFreeList->endOffset);
  freeList->fileLocation = root.freeList;
  
  // Simply enter all block references as free memory blocks into the free list
  for (auto it = fileFreeList->begin(), end = fileFreeList->end(freeList->fileLocation.offset); it != end; ++it) {
    freeList->FreeMemoryBlock(*it);
  }

  // We assume the free list to already be reorganized before writing it to file, so we won't do this here


  // Finally assign the loaded snapshot and free list to this database
  this->snapshot = std::move(snapshot);
  this->freeList = std::move(freeList);
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


Database::CommitResult::CommitResult(Database& database, std::unique_ptr<Snapshot> snapshot, std::unique_ptr<FreeList> freeList) 
  : database(database), snapshot(std::move(snapshot)), freeList(std::move(freeList)) 
{}

commit_id Database::CommitResult::ApplyToDatabase() {
  // As inner class we can directly access the database snapshot

  TODO("Wait for the necessary file writes to finish before modifying this pointer!");


  database.snapshot = std::move(snapshot);
  database.freeList = std::move(freeList);
  return database.snapshot->commitId;
}

Database::CommitResult Database::CalculateCommitResult(network::MessagePointer_T<network::message::TransactionCommit>* commitPos, network::MessagePointer_T<network::message::TransactionCommit>* commitEnd) {
  // Create the new snapshot implicitly also setting the new commit id
  auto newSnapshot = std::make_unique<Snapshot>(*snapshot);
  auto newFreeList = freeList ? std::make_unique<FreeList>(*freeList) : nullptr;
  auto releasedList = freeList ? std::make_unique<FreeList>(0) : nullptr;

  // Now apply the commit messages one by one to the snapshot modifying it in the process
  for (auto pos = commitPos, end = commitEnd; pos != end; ++pos) {
    newSnapshot->ApplyCommitMessage(**pos, newFreeList.get(), releasedList.get());
  }

  if (newFreeList) {
    // Calculate the new free lists file maximum size AFTER merging it with the blocks from the released list and allocate that amount of memory
    // from the current free list BEFORE merging with the released list to avoid allocating from any recently released blocks as this would overwrite
    // these blocks before finalizing the transaction.
    newFreeList->fileLocation.size = newFreeList->EstimateRequiredSize(*releasedList);
    newFreeList->fileLocation.offset = newFreeList->AllocateMemoryBlock(newFreeList->fileLocation.size);

    // Only now merge the free list with released blocks and reorganize it
    for (auto location : *releasedList) {
      newFreeList->FreeMemoryBlock(location);
    }
    newFreeList->Reorganize();
    TODO("Schedule write freelist operation");
  }
  
  return CommitResult(*this, std::move(newSnapshot), std::move(newFreeList));
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



Database::Snapshot::Snapshot(commit_id commitId) : commitId(commitId), nextFreeSegmentId(0), nextFreeSegmentIdBlob(constants::NextFreeBlobId, commitId) {
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


void Database::Snapshot::ApplyCommitMessage(network::message::TransactionCommit& commitMessage, FreeList* allocateFrom, FreeList* releaseInto) {
  TODO("Maybe we should use if(constexpr) for the db-less path");

  // Apply each modification in sequence
  for (auto pos = commitMessage.begin(), end = commitMessage.end(); pos != end; ++pos) {
    auto& update = *pos;

    TODO("Keep track of deleted memory blocks");

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
      TODO("Create the default cluster and blob in here too");

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
        TODO("Create the default blob in here too if necessary")

        if (update.blob == constants::NextFreeBlobId) {
          // One or more blobs are being created in this cluster -> update the next free blob id of the cluster
          cluster->SetNextFreeBlobId(pos.ReadId<blob_id>());
        } else if (update.blobSize == constants::DeleteBlobSize) {
          // The specified blob is being deleted
          cluster->DeleteBlob(update.blob);
        } else {
          // Regular blob content update
          TODO("I know how I can allocate memory from the freelist for the new blob, but what about the modified cluster (the blob list changed)...");
          TODO("I should probably just collect them in a list and mark them as (needs reallocation) or smth like this and allocate memory for them in the end");
          TODO("And with in the end I mean after applying ALL commit messages to not attempt to allocate the memory multiple times");
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

  TODO("When writing the snapshot into file, assert that we don't write past the calculated required size");
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



auto Database::FreeList::begin() -> iterator {
  return freeList.begin();
}

auto Database::FreeList::end() -> iterator {
  return freeList.end();
}



uint64_t Database::FreeList::EstimateRequiredSize(const FreeList& other) const {
  // Maximum size not taking into account that Blocks may be deleted inside Reorganize()
  auto estimatedSize = CalculateRequiredSize() + sizeof(file::BlockReference) * other.freeList.size();

  // If the current free list has no free memory, then it will have to allocate a new block from the end of the file
  // which will be exactly the required size, so no new block reference will be added to the free list as it will be immediately consumed.
  return estimatedSize;
}



TODO("Write tests for freelist allocation and reorganization")


}}
