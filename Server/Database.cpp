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
  TODO("check for lock conflicts with other clients for all requested blobs");
  TODO("set locks if no conflicts");
  TODO("return true if successful");
  TODO("what if the blobs we are trying to lock don't exist? This should be communicated back to the caller too!");
  return true;
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
