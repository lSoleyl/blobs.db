#include "pch.hpp"
#include "Database.hpp"

namespace blobs {
namespace server {

std::map<std::string, Database, std::less<>> Database::databases;

Database::Database(std::string name) : name(std::move(name)), lastSegmentId(0), commitId(1) {
  segments.emplace(0, std::make_unique<Segment>(0));
}

Database& Database::Get(std::string_view databaseName) {
  auto pos = databases.find(databaseName);
  if (pos != databases.end()) {
    return pos->second;
  } 

  std::string nameStr(databaseName.data(), databaseName.size());
  return databases.emplace(nameStr, Database(nameStr)).first->second;
}

Blob* Database::GetBlob(const BlobLocation& location) {
  if (auto segment = GetSegment(location.segment)) {
    if (auto cluster = segment->GetCluster(location.cluster)) {
      if (auto blob = cluster->GetBlob(location.blob)) {
        return blob;
      }
    }
  }
  return nullptr;
}


Segment* Database::GetSegment(segment_id segment) {
  auto pos = segments.find(segment);
  return (pos != segments.end()) ? pos->second.get() : nullptr;
}

bool Database::AcquireLocks(const network::message::BlobsRead& message) {
  auto client = message.clientId;
  bool write = message.writeLock;

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

bool Database::CanClientAcquireLock(client_id client, const BlobLocation& location, bool write) {
  TODO("Also handle special locking rules: When client attempts to lock cluster's blob table, no write locks on any blob may exist in that cluster!");

  auto pos = locks.find(location);
  if (pos == locks.end()) {
    return true; // no lock yet at that location -> lock is possible
  }

  auto& lock = *pos;
  if (!write) {
    // A read lock can be acquired if no write lock is held or the write lock is being held by this client
    return !lock.write || *lock.write == client;

  } else {
    // A write lock can only be acquired if this client is the only client holding the read lock (or no client)
    if (lock.write) {
      // If this is already write locked, then acquiring can only work if this client is already holding the write lock
      return *lock.write == client;
    }

    // Either there are no read locks or this client holds the only read lock
    return lock.read.empty() || (lock.read.size() == 1 && lock.read[0] == client);
  }
}


void Database::AcquireClientLock(client_id client, const BlobLocation& location, bool write) {
  auto pos = locks.find(location);
  if (pos == locks.end()) {
    // Simple case, nobody requested a lock for this location yet
    Lock lock(location);
    if (write) {
      lock.write = client;
    } else {
      lock.read.push_back(client);
    }
    locks.insert(std::move(lock));
  } else {
    // Lock already exists
    auto& lock = const_cast<Lock&>(*pos); // const_cast is safe here, because we don't modify the ordering criterion
    if (write) {
      // Set write lock and potentially upgrade read lock
      lock.write = client;
      lock.read.clear(); // no read locks can exists while a write lock exists
    } else if (lock.write == client) {
      // Nothing to do... we don't downgrade write locks into read locks
    } else {
      // Acquire a read lock
      auto pos = std::find(lock.read.begin(), lock.read.end(), client);
      if (pos == lock.read.end()) {
        // The client didn't hold this read lock before
        lock.read.push_back(client);
      }
    }
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

void Database::ReleaseLocks(client_id client, const std::vector<BlobLocation>& locations) {
  for (auto& location : locations) {
    auto lock = locks.find(location);
    assert(lock != locks.end()); // This would indicate a programming error as the client assumes a lock which he doesn't has.
    // The const_cast here is valid as we do not modify the sorting order in any respect
    bool empty = const_cast<Lock&>(*lock).Release(client);
    // Should we delete this Lock object now or keep it in memory forever? What is worse performance wise?
  }
}



}}
