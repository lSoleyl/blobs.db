#include "pch.hpp"
#include <blobs/Transaction.hpp>
#include <blobs/Exception.hpp>
#include <common/BlobLocation.hpp>

#include <set>
#include <map>

namespace blobs {

struct Transaction::State {
  struct DatabaseState {
    struct HeldLocks {
      std::set<BlobLocation> read, write;
    };

    HeldLocks heldLocks;
    std::map<BlobLocation, std::vector<uint8_t>> writtenBlobs;
    std::set<BlobLocation> deletedBlobs;
  };

  std::map<database_id, DatabaseState> forDatabase;
};

uint64_t Transaction::nextId = 0;
std::unique_ptr<Transaction> Transaction::current;

Transaction::Transaction() : id(nextId++), state(new State) {}


bool Transaction::IsRunning() {
  return !!current;
}

bool Transaction::Commit() {
  if (!current) {
    return false;
  }

  // 1. Construct commit message(s) from the transaction state
  // 2. Send message(s) to the server
  // 3. Await response
  // 4. Update our local blob cache somehow

  TODO("Implement commit");
}


bool Transaction::Abort() {
  if (!current) {
    return false;
  }

  TODO("Implement abort");
}


void Transaction::AbortDeadlock() {
  // Simply delete this transaction to clear up all locks and scheduled writes.
  Transaction::current.reset();
}


Transaction* Transaction::Get(bool startIfNotActive) {
  if (!current && startIfNotActive) {
    current.reset(new Transaction);
  }

  return current.get();
}


Transaction::LockMode Transaction::GetLockType(database_id dbId, const BlobLocation& location) const {
  auto& dbState = state->forDatabase;
  auto pos = dbState.find(dbId);
  if (pos != dbState.end()) {
    auto& locks = pos->second.heldLocks;
    if (locks.write.find(location) != locks.write.end()) {
      return LockMode::Write;
    } 

    if (locks.read.find(location) != locks.read.end()) {
      return LockMode::Read;
    }
  }

  return LockMode::None;
}

void Transaction::AcquiredLock(database_id dbId, const BlobLocation& location, LockMode lock) {
  assert(lock != LockMode::None); // Why would anyone call it like this!?
  auto& dbLocks = state->forDatabase[dbId].heldLocks;
  
  // We simply insert without checking for any invariants. Having both a write and read lock will be recognized as simply
  // having a write lock by GetLockType() we thus don't have to worry about removing a read lock when upgrading to a write lock
  if (lock == LockMode::Read) {
    dbLocks.read.insert(location);
  } else if (lock == LockMode::Write) {
    dbLocks.write.insert(location);
  }
}



void Transaction::WriteBlob(database_id dbId, const BlobLocation& location, const void* blobData, blob_size blobSize) {
  auto& dbState = state->forDatabase[dbId];
  if (dbState.deletedBlobs.find(location) != dbState.deletedBlobs.end()) {
    // Cannot write a blob after deleting it in the same transaction
    throw exception::BlobDeleted();
  }

  // Store the new blob data in our transaction state
  auto& blobVector = dbState.writtenBlobs[location];
  blobVector.resize(blobSize);
  std::copy_n(static_cast<const uint8_t*>(blobData), blobSize, blobVector.begin());
}


void Transaction::DeleteBlob(database_id dbId, const BlobLocation& location) {
  auto& dbState = state->forDatabase[dbId];

  // If we first called WriteBlob() and then in the same transaction DeleteBlob(),
  // then clear the write data and mark the blob as deleted.
  dbState.writtenBlobs.erase(location);

  // We could check for double deletion, but what benefit would this added strictness have? 
  // There is no harm to any side if we simply ignore such an error since there is no loss of data.
  dbState.deletedBlobs.insert(location);
}

std::optional<std::pair<const void*, blob_size>> Transaction::ReadBlob(database_id dbId, const BlobLocation& location) const {
  auto dbPos = state->forDatabase.find(dbId);
  if (dbPos == state->forDatabase.end()) {
    return std::nullopt;
  }

  auto& dbState = dbPos->second;
  if (dbState.deletedBlobs.find(location) != dbState.deletedBlobs.end()) {
    // Attempt to read an already deleted blob
    throw exception::BlobDeleted();
  }

  auto writePos = dbState.writtenBlobs.find(location);
  if (writePos != dbState.writtenBlobs.end()) {
    // Blob has been written in this transaction -> return the written content
    return std::make_pair(static_cast<const void*>(writePos->second.data()), static_cast<blob_size>(writePos->second.size()));
  }

  // Blob not written in this transaction yet
  return std::nullopt;
}






}

