#include "pch.hpp"
#include "include/server/Database.hpp"
#include "include/server/Server.hpp"
#include "include/server/File.hpp"

namespace blobs {
namespace server {

std::map<std::string, Database, std::less<>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), useCount(0), loaded(false) {
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
  TODO("In some scenarios the database load thread may not be done yet.");
  file.Close();

  // Delete this object by removing it from the databases map
  databases.erase(name);
}



void Database::InitializeInMemory() {
  // Create the snapshot with segment cluster and blob
  auto snapshot = std::make_unique<Snapshot>();
  auto segment = snapshot->UpdateSegment(0, nullptr);
  snapshot->SetNextFreeSegmentId(1);

  // Cluster 0 and Blob 0 are created implicitly
  this->snapshot = std::move(snapshot);
  loaded = true;
}



void Database::LoadFromFile() {
  TODO("Maybe we should just load the database in the main thread... I mean how often do we open a new database? Is it really worth it?");


  TODO("Maybe this should be the Database-Thread and we simply create one thread per database, which exclusively performs read/write operations on the database");
  TODO("This could become the main IO thread for the database and we could also post tasks to it via an IOCompletionPort to process if the thread is idle");
  std::thread loadThread([this]() {
    try {
      bool exists;
      file = FileBackend::OpenExclusive(name.c_str(), exists);
      if (!file) {
        TODO("Check what the reason was and translate the most common ones into own error codes to return");
        throw std::exception("Failed to open/create database file");
      }

      if (!exists) {
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
  for (auto& blockReference : *freeList) {
    freeList->FreeMemoryBlock(blockReference);
  }

  // We assume the free list to already be reorganized before writing it to file, so we won't do it here


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
  return snapshot->GetBlob(location, file);
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


Database::CommitResult::CommitResult(Database& database, std::unique_ptr<Snapshot> snapshot, std::unique_ptr<FreeList> freeList, std::unique_ptr<MemoryBlockDelta> delta)
  : database(database), snapshot(std::move(snapshot)), freeList(std::move(freeList)), delta(std::move(delta))
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


  if (delta) {
    // Mark the new snapshot as allocated and the current one as released
    delta->Allocated(newSnapshot.get());
    delta->Released(snapshot.get());
  }

  // Now apply the commit messages one by one to the snapshot modifying it in the process
  for (auto pos = commitPos, end = commitEnd; pos != end; ++pos) {
    newSnapshot->ApplyCommitMessage(**pos, delta.get());
  }

  if (newFreeList) {
    newFreeList->AllocateAndApplyDelta(*delta);
  }
  
  return CommitResult(*this, std::move(newSnapshot), std::move(newFreeList), std::move(delta));
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




Blob* Database::Snapshot::GetBlob(const BlobLocation& location, const FileBackend& file) {
  if (location.segment == constants::NextFreeSegmentId && location.cluster == constants::NextFreeClusterId && location.blob == constants::NextFreeBlobId) {
    // Return special blob holding the next free segment id
    return &nextFreeSegmentIdBlob;
  }
  
  // Otherwise perform regular lookup
  if (auto segment = GetSegment(location.segment)) {
    // Load the segment from file if it isn't loaded yet
    TODO("Once we use async IO to load stuff, we must handle LOADED and LOADING separately");
    if (segment->status != Status::LOADED) {
      segment->LoadFrom(file);
      assert(segment->status == Status::LOADED);
    }

    // Use GetBlob() to support `NextFreeClusterId`
    return segment->GetBlob(location.cluster, location.blob, file);
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


void Database::Snapshot::ApplyCommitMessage(network::message::TransactionCommit& commitMessage, MemoryBlockDelta* delta) {
  TODO("Maybe we should use if(constexpr) for the db-less path");

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

      } else {
        // Fetch a copy of the cluster this update refers to and create it if necessary
        auto cluster = segment->UpdateCluster(update.cluster, delta);

        if (update.blob == constants::NextFreeBlobId) {
          // One or more blobs are being created in this cluster -> update the next free blob id of the cluster
          cluster->SetNextFreeBlobId(pos.ReadId<blob_id>());
        } else if (update.blobSize == constants::DeleteBlobSize) {
          // The specified blob is being deleted
          cluster->DeleteBlob(update.blob, delta);
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
