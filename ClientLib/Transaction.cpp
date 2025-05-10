#include "pch.hpp"
#include <blobs/Transaction.hpp>
#include <common/BlobLocation.hpp>

#include <set>
#include <map>

namespace blobs {

struct Transaction::State {
  struct HeldLocks {
    std::set<BlobLocation> read, write;
  };

  std::map<database_id, HeldLocks> heldLocks;
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
  auto& heldLocks = state->heldLocks;
  auto pos = heldLocks.find(dbId);
  if (pos != heldLocks.end()) {
    auto& locks = pos->second;
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
  auto& dbLocks = state->heldLocks[dbId];
  
  // We simply insert without checking for any invariants. Having both a write and read lock will be recognized as simply
  // having a write lock by GetLockType() we thus don't have to worry about removing a read lock when upgrading to a write lock
  if (lock == LockMode::Read) {
    dbLocks.read.insert(location);
  } else if (lock == LockMode::Write) {
    dbLocks.write.insert(location);
  }
}



void Transaction::WriteBlob(database_id dbId, const BlobLocation& location, const void* blobData, blob_size blobSize) {
  TODO("Store the blob data in the internal transaction state for commit");
}



}

