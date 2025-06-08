#include "pch.hpp"
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
#include <blobs/Transaction.hpp>
#include <internal/Network.hpp>
#include <network/Client.hpp>

#include <network/message/All.hpp>
#include <common/BlobLocation.hpp>

#include <charconv>

namespace blobs {

class Database::BlobCache {
public:
  struct CachedBlob {
    std::vector<uint8_t> data;
    commit_id lastUpdated; // commit id when the blob was last updated (according to the cache)
    uint64_t transactionId; // the id of the client transaction when this blobs was last written to the cache
    TODO("add some LRU counter to decache rarely used blobs and save memory?")

      std::pair<const void*, blob_size> Data() const {
      return std::pair<const void*, blob_size>(data.data(), static_cast<blob_size>(data.size()));
    }
  };

  /** Read a cached blob from the cache or return nullptr if no blob is cached
   */
  CachedBlob* Get(const BlobLocation& location) {
    auto pos = cache.find(location);
    return (pos != cache.end()) ? &pos->second : nullptr;
  }

  /** Update stored blob with the specified data and commit id. This is used during transaction commit.
   */
  void Update(const BlobLocation& location, std::vector<uint8_t> data, commit_id lastUpdated, uint64_t transactionId) {
    auto& cachedBlob = cache[location];
    cachedBlob.data = std::move(data);
    cachedBlob.lastUpdated = lastUpdated;
    cachedBlob.transactionId = transactionId;
  }


  /** Store/Update blob data in the cache and return a reference to the stored blob
   */
  CachedBlob& Set(const BlobLocation& location, const void* data, blob_size size, commit_id lastUpdated, uint64_t transactionId) {
    auto& cachedBlob = cache[location];
    cachedBlob.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    cachedBlob.lastUpdated = lastUpdated;
    cachedBlob.transactionId = transactionId;
    return cachedBlob;
  }

  /** Simply removes a cached blob location from the blob cache
   */
  void RemoveBlob(const BlobLocation& location) {
    cache.erase(location);
  }

  /** Removes all cached blobs in the specified cluster (used during cluster deletion)
   */
  void RemoveCluster(segment_id segment, cluster_id cluster) {
    // This operation is not very efficient due to us using an unordered map here, but switch to a regular map
    // just to speed up this rather rare operation doesn't justify the cost.

    // First collect all entries to be deleted
    std::vector<BlobLocation> toDelete;
    for (auto& [location, _] : cache) {
      if (location.segment == segment && location.cluster == cluster) {
        toDelete.push_back(location);
      }
    }

    // Then erase them one by one
    for (auto& location : toDelete) {
      cache.erase(location);
    }
  }

  /** Removes all cached blobs in the specified segment (used during segment deletion)
   */
  void RemoveSegment(segment_id segment) {
    // This operation is not very efficient due to us using an unordered map here, but switch to a regular map
    // just to speed up this rather rare operation doesn't justify the cost.

    // First collect all entries to be deleted
    std::vector<BlobLocation> toDelete;
    for (auto& [location, _] : cache) {
      if (location.segment == segment) {
        toDelete.push_back(location);
      }
    }

    // Then erase them one by one
    for (auto& location : toDelete) {
      cache.erase(location);
    }
  }

private:
  std::unordered_map<BlobLocation, CachedBlob> cache;
};




Database::Database(std::string name, database_id id, connection_id connectionId) : name(std::move(name)), id(id), connectionId(connectionId), cache(new BlobCache) {}

Database::~Database() {}


Database* Database::Open(const char* connectionStringBegin, size_t connectionStringLen) {
  std::string_view connectionString(connectionStringBegin, connectionStringLen);

  auto slashPos = connectionString.find('/');
  if (slashPos == std::string_view::npos) {
    // Database name is mandatory
    throw Exception("No database name defined in connection string!");
  }

  auto hostName = connectionString.substr(0, slashPos); // everything up to but not including the '/'
  auto databaseName = connectionString.substr(slashPos + 1); // everything following the '/'
  int port = 8108; // default port (spells 'blob')

  // Now look for a colon before the first slash (a colon following the slash will be ignored)
  auto colonPos = hostName.find(':');
  if (colonPos != std::string_view::npos) {
    auto portString = hostName.substr(colonPos + 1); // everything following the ':'
    hostName = hostName.substr(0, colonPos); // everything up to but not including the ':'
    
    auto result = std::from_chars(portString.data(), portString.data() + portString.size(), port);
    if (result.ptr != portString.data() + portString.size()) {
      throw Exception("Failed to parse '" + std::string(portString) + "' as port number!");
    }
  }

  return Open(hostName, databaseName, port);
}



Database* Database::Open(const char* hostNameData, size_t hostNameLen, const char* databaseNameData, size_t databaseNameLen, int port) {
  std::string_view hostName(hostNameData, hostNameLen);
  std::string_view databaseName(databaseNameData, databaseNameLen);

  // Get the connection to the database server (open or reuse)
  auto connectionId = internal::Network::Get(hostName, port);

  auto& client = internal::Network::Get(connectionId);
  client.SendDatabaseOpen(databaseName);

  // Await the DatabaseOpenResponse
  auto message = internal::Network::ExpectMessage<network::message::DatabaseOpenResponse>(client);
  
  if (message->result == network::message::DatabaseOpenResponse::Result::SUCCESS) {
    return new Database(std::string(databaseName), message->databaseId, connectionId);
  } else if (message->result == network::message::DatabaseOpenResponse::Result::DATABASE_NOT_FOUND) {
    throw blobs::Exception("Database not found!");
  } else if (message->result == network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN) {
    throw blobs::Exception("Too many databases already open. Close unused databases and retry.");
  }
  
  assert(false); // unhandled result type
  // not reached
  return nullptr;
}



std::pair<const void*, blob_size> Database::ReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock) {
  TODO("Add synchronization: Only one thread may communicate with the database at any given time.");

  // Get the currently running transaction or start a new one if no is running yet
  auto transaction = Transaction::Get(connectionId, true);

  BlobLocation location(segment, cluster, blob);

  // First check whether we have already written this blob in the current transaction and return the written data if so
  // This will also ensure that we don't read an already deleted blob and throw an exception if so
  if (auto blobContent = transaction->ReadBlob(id, location)) {
    return *blobContent;
  }

  auto cachedBlob = cache->Get(location);

  if (cachedBlob->transactionId == transaction->id) {
    // We already read this blob. Now if we also aready hold a compatible lock to the requested one, then we can simply return the cached blob
    auto currentLock = transaction->GetLockType(id, location);
    if (currentLock >= (writeLock ? Transaction::LockMode::Write : Transaction::LockMode::Read)) {
      // Our current lock is already sufficient to fullfill the request -> return the cached blob content
      return cachedBlob->Data();
    } else if (currentLock == Transaction::LockMode::Read) {
      // We want to upgrade our read lock to a write lock -> upgrade the lock and return the cached blob content
      WriteLockNoContent(location);
      return cachedBlob->Data();
    }
  }

  // Request the blob from the server
  auto& client = internal::Network::Get(connectionId);
  auto request = network::message::BlobsRead::Create(id, 1, writeLock ? network::message::BlobsRead::LockMode::Write : network::message::BlobsRead::LockMode::Read);
  auto& address = *request->begin();
  address = location;
  address.ifCommitIdHigher = cachedBlob ? cachedBlob->lastUpdated : 0;
  client.SendMessageToServer(std::move(request));
  
  // Wait for the response and handle it
  auto response = internal::Network::ExpectMessage<network::message::BlobsReadResponse>(client);
  if (response->result == network::message::BlobsReadResponse::Result::SUCCESS) {
    if (cachedBlob && response->nBlobs == 0) {
      // Our cached blob is up to date, but we still successfully acquired the lock -> notify the transaction of the lock
      transaction->AcquiredLock(id, location, writeLock ? Transaction::LockMode::Write : Transaction::LockMode::Read);
      return cachedBlob->Data();
    } else if (response->nBlobs == 1) {
      // Server has responded with a newer version of the blob, or we don't have it in our cache yet
      auto& blobData = *response->begin();
      auto& cachedBlob = cache->Set(location, blobData.Data(), blobData.blobSize, blobData.commitId, transaction->id);
      transaction->AcquiredLock(id, location, writeLock ? Transaction::LockMode::Write : Transaction::LockMode::Read);
      return cachedBlob.Data();
    } else {
      assert(false); // server repsonsed with an illegal number of blobs!
    }
  } else {
    // Handle error response
    HandleReadBlobErrorResponse(*response);
  }

  // not reached
  return std::make_pair(nullptr, 0);
}



void Database::WriteBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize) {
  if (blobSize > constants::MaxBlobSize) {
    // Make sure, the client cannot write blobs, which are larger than the supported maximum
    throw exception::BlobTooLarge(blobSize);
  }

  // Get the currently running transaction or start a new one if no is running yet
  auto transaction = Transaction::Get(connectionId, true);
  BlobLocation location(segment, cluster, blob);


  if (transaction->GetLockType(id, location) != Transaction::LockMode::Write) {
    // We don't have the write lock yet -> acquire it
    // Since we want to write the blob, we don't have to read the content, we just want the write lock
    WriteLockNoContent(location);
  }

  // Store the new blob data for the transaction commit in the transaction state.
  // The following call will throw an exception if we attempt to write a blob, which has been deleted in this transaction.
  transaction->WriteBlob(id, location, blobData, static_cast<blob_size>(blobSize));
}


blob_id Database::CreateBlob(segment_id segment, cluster_id cluster, const void* blobData, size_t blobSize) {
  // We perform this size check before actually creating the blob or else the exception is thrown after the blob 
  // has already been logically created, so we sadly have to perform this size check twice, but the alternative would be worse.
  if (blobSize > constants::MaxBlobSize) {
    throw exception::BlobTooLarge(blobSize);
  }

  // Create the new blob and directly write the data into it
  auto blobId = CreateBlobInternal(segment, cluster);
  WriteBlobInternal(segment, cluster, blobId, blobData, blobSize);

  // Finally return the blob id to the caller so he knows, which blob has been created.
  return blobId;
}



void Database::DeleteBlob(segment_id segment, cluster_id cluster, blob_id blob) {
  // Get the currently running transaction or start a new one if no is running yet
  auto transaction = Transaction::Get(connectionId, true);
  BlobLocation location(segment, cluster, blob);

  // Acquire a write lock for deletion of the blob if we don't already hold one.
  if (transaction->GetLockType(id, location) != Transaction::LockMode::Write) {
    WriteLockNoContent(location); // no need to fetch the blob content -> we delete the blob anyway
  }

  // Validate that the cluster isn't deleted yet and then mark the blob for deletion upon transaction commit
  transaction->DeleteBlob(id, location);

  // Remove the blob from our blob cache
  cache->RemoveBlob(location);
}


void Database::DeleteCluster(segment_id segment, cluster_id cluster) {
  // Get the currently running transaction or start a new one if no is running yet
  auto transaction = Transaction::Get(connectionId, true);
  BlobLocation lockLocation(segment, cluster, constants::ClusterDeleteId);

  // Acquire a write lock for deletion of the cluster if we don't already hold one (which is unlikely)
  if (transaction->GetLockType(id, lockLocation) != Transaction::LockMode::Write) {
    WriteLockNoContent(lockLocation); // no need to fetch the blob content -> we only need to write into it
  }

  // Validate that the segment isn't deleted yet and then mark the cluster with all its segments
  // for deletion upon transaction commit
  transaction->DeleteCluster(id, segment, cluster);

  // Remove all cached blobs for that cluster
  cache->RemoveCluster(segment, cluster);
}


void Database::DeleteSegment(segment_id segment) {
  // Get the currently running transaction or start a new one if no is running yet
  auto transaction = Transaction::Get(connectionId, true);
  BlobLocation lockLocation(segment, constants::SegmentDeleteId, constants::ClusterDeleteId);

  // Acquire a write lock for deletion of the segment if we don't already hold one (which is unlikely)
  if (transaction->GetLockType(id, lockLocation) != Transaction::LockMode::Write) {
    WriteLockNoContent(lockLocation); // no need to fetch the blob content -> we only need to write into it
  }

  // Mark the segment for deletion upon transaction commit
  transaction->DeleteSegment(id, segment);

  // Remove all cached blobs for that segment
  cache->RemoveSegment(segment);
}



void Database::Close() {
  if (Transaction::IsRunning()) {
    throw exception::DbCloseDuringTxn(name);
  }

  auto& client = internal::Network::Get(connectionId);
  client.SendDatabaseClose(id);
  auto message = internal::Network::AwaitMessage(client);
  if (auto confirmation = message.Get<network::message::DatabaseClose>()) {
    // Server confirmed closing of this database -> release connection and delete this database instance
    internal::Network::Release(connectionId);  
  } else if (auto closed = message.Get<network::message::ConnectionClosed>()) {
    // Server confirmed close of last database by simply closing the connection
    internal::Network::ServerClosedConnection(connectionId);
  } else {
    throw Exception("Unexpected server response to DatabaseClose");
  }

  delete this;
}


void Database::UpdateCacheForCommittedBlob(const BlobLocation& location, std::vector<uint8_t> data, commit_id commitId, uint64_t transactionId) {
  cache->Update(location, std::move(data), commitId, transactionId);
}


blob_id Database::CreateBlobInternal(segment_id segment, cluster_id cluster) {
  // Acquire a write lock on the NextFreeBlobId id, which will allow us to create blobs in this cluster and
  // also tell us the next blob id to use.
  auto [data, size] = ReadBlobInternal(segment, cluster, constants::NextFreeBlobId, true);
  assert(size == sizeof(blob_id)); // This blob only consists of the blob_id value

  BlobLocation newLocation(segment, cluster, *static_cast<const blob_id*>(data));
  if (newLocation.blob > constants::MaxBlobId) {
    // Cluster is full, no more blobs an be created
    throw exception::BlobLimitReached(segment, cluster);
  }

  // Update the NextFreeBlobId blob
  blob_id nextBlobId = newLocation.blob + 1;
  WriteBlobInternal(segment, cluster, constants::NextFreeBlobId, &nextBlobId, sizeof(blob_id));

  // The blob is now logically created, so we also implicitly hold a write lock to it
  auto transaction = Transaction::Get(connectionId, false); // ReadBlobInternal() has already started a transaction if not already
  transaction->AcquiredLock(id, newLocation, Transaction::LockMode::Write);

  // Now we logically created the blob and we implicitly created the write lock, now the caller just has to actually write some
  // blob data for this blob or else it won't actually be created on transaction commit.
  return newLocation.blob;
}


void Database::WriteLockNoContent(const BlobLocation& location) {
  // Create the request for this blob
  auto transaction = Transaction::Get(connectionId, true);
  auto& client = internal::Network::Get(connectionId);
  auto request = network::message::BlobsRead::Create(id, 1, network::message::BlobsRead::LockMode::Delete);
  auto& address = *request->begin();
  address = location;

  // Send it and await the server's response
  client.SendMessageToServer(std::move(request));
  auto response = internal::Network::ExpectMessage<network::message::BlobsReadResponse>(client);
  
  // Handle response
  if (response->result == network::message::BlobsReadResponse::Result::SUCCESS) {
    assert(response->nBlobs == 0); // The server should never respond with a blob to a Delete request
    // Nothing to enter into the cache, but update the held lock type in the transaction
    transaction->AcquiredLock(id, location, Transaction::LockMode::Write);
  } else {
    // Handle error response
    HandleReadBlobErrorResponse(*response);
  }
}



void Database::HandleReadBlobErrorResponse(const network::message::BlobsReadResponse& response) {
  assert(response.result != network::message::BlobsReadResponse::Result::SUCCESS);

  TODO("Define separate errors for SEGMENT_DOES_NOT_EXIST, CLUSTER_DOES_NOT_EXIST, BLOB_DOES_NOT_EXIST for better error handling");

  switch (response.result) {
    case network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST:
      throw Exception("Requested blob does not exist!");

    case network::message::BlobsReadResponse::Result::DATBASE_NOT_OPENED:
      // This should never happen unless the client lib doesn't track database open/closes correctly
      throw Exception("internal error: Database not opened!");

    case network::message::BlobsReadResponse::Result::LOCK_TIMEOUT:
      TODO("somehow add more detail to this error...");
      throw exception::LockTimeout();

    case network::message::BlobsReadResponse::Result::DEADLOCK:
      if (auto transaction = Transaction::Get(connectionId, false)) {
        // Delete the currently running transaction and if we are connected to other servers -> notify them about the transaction abort of this client
        transaction->AbortDeadlock();
      } else {
        // I cannot reall imagine a valid scenario, where the client has no transaction for a connection and recieves a BlobsReadResponse.
        // Because reads are performed in a strict serial order, it cannot be a timing issue of one server triggering a deadlock abort and
        // the other server responding to a previous request not yet knowing about the abort.
        assert(false);
      }
      TODO("somehow add more detail to this error...");
      throw exception::Deadlock();

    default:
      throw Exception("Server sent unknown result code!");
  }
}



}