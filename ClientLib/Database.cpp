#include "pch.hpp"
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
#include <blobs/Transaction.hpp>
#include <internal/Network.hpp>
#include <internal/HeldLocks.hpp>
#include <internal/DatabasesState.hpp>
#include <network/ClientInterface.hpp>

#include <network/message/All.hpp>
#include <common/BlobLocation.hpp>
#include <common/Encoding.hpp>

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


  /** Updates the transaction id for all blob specified in locks to the passed transaction id. This is used when first accessing 
   *  a database inside a new transaction to ensure that the blobs for which we kept sticky locks from the last transaction will be directly
   *  read from cache in the next transaction instead of asking the server. (That is the whole point of sticky locks)
   */
  void UpdateTxnIdForHeldLocks(const internal::HeldLocks& locks, uint64_t transactionId) {
    for (auto& lock : locks.read) {
      if (auto blob = Get(lock)) {
        blob->transactionId = transactionId;
      }
    }

    for (auto& lock : locks.write) {
      if (auto blob = Get(lock)) {
        blob->transactionId = transactionId;
      }
    }
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




Database::Database(const Session::Handle& session, std::string name, database_id id, connection_id connectionId) : name(std::move(name)), id(id), connectionId(connectionId), cache(new BlobCache), session(session) {
  assert(session->OwnsLock());
  session->Databases().openedDatabases.emplace(id, this);
}

Database::~Database() {
  assert(!session->OwnsLock()); // because we unlock it right before deletion to avoid holding past the session's deletion (in case the database is the last object referencing the session)
  session->Databases().openedDatabases.erase(id);
}


Database* Database::Open(const Session::Handle& session, const char* connectionStringBegin, size_t connectionStringLen) {
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

  return Open(session, hostName, databaseName, port);
}


Database* Database::Open(const Session::Handle& session, const wchar_t* connectionString, size_t connectionStringLen) {
  // Simply encode into UTF-8 and then call the regular version
  std::string u8ConnectionString = encoding::ToUTF8(std::wstring_view(connectionString, connectionStringLen));
  return Open(session, u8ConnectionString.data(), u8ConnectionString.length());
}


Database* Database::Open(const Session::Handle& session, const char* hostNameData, size_t hostNameLen, const char* databaseNameData, size_t databaseNameLen, int port) {
  std::string_view hostName(hostNameData, hostNameLen);
  std::string_view databaseName(databaseNameData, databaseNameLen);

  auto sessionLock = session->Lock();
  auto& network = session->Network();
  // Get the connection to the database server (open or reuse)
  auto connectionId = network.Get(hostName, port);

  auto& client = network.Get(connectionId);
  client.SendMessageToServer(network::message::DatabaseOpen::Create(databaseName));

  // Await the DatabaseOpenResponse
  auto message = network.ExpectMessage<network::message::DatabaseOpenResponse>(client);
  
  if (message->result == network::message::DatabaseOpenResponse::Result::SUCCESS) {
    return new Database(std::move(session), std::string(databaseName), message->databaseId, connectionId);
  }
  
  // Opening the database failed for some reason -> release the network connection to not leak it since
  // no database has been created to hold on to it.
  network.Release(connectionId);
  if (message->result == network::message::DatabaseOpenResponse::Result::DATABASE_OPEN_FAILED) {
    throw Exception("Failed to open database!");
  } else if (message->result == network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN) {
    throw Exception("Too many databases already open. Close unused databases and retry.");
  } else if (message->result == network::message::DatabaseOpenResponse::Result::DATABASE_ALREADY_OPEN) {
    throw exception::DbAlreadyOpen(std::string(databaseName));
  }
  
  assert(false); // unhandled result type
  // not reached
  return nullptr;
}

Database* Database::Open(const Session::Handle& session, const char* hostName, size_t hostNameLen, const wchar_t* databaseName, size_t databaseNameLen, int port) {
  // Simply convert the UTF-16 database name into UTF-8 and call the regular Open() function
  auto u8DatabaseName = encoding::ToUTF8(std::wstring_view(databaseName, databaseNameLen));
  return Database::Open(session, hostName, hostNameLen, u8DatabaseName.data(), u8DatabaseName.length(), port);
}



std::pair<const void*, blob_size> Database::ReadBlob(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock) {
  if (segment > constants::MaxSegmentId) {
    throw Exception("Invalid segment id");
  }

  if (cluster > constants::MaxClusterId) {
    throw Exception("Invalid cluster id");
  }

  if (blob > constants::MaxBlobId) {
    throw Exception("Invalid blob id");
  }

  // Only one thread at a time may read a blob
  auto sessionLock = session->Lock();
  return ReadBlobInternal(segment, cluster, blob, writeLock);
}


std::pair<const void*, blob_size> Database::ReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, bool writeLock) {
  assert(session->OwnsLock());

  // Get the currently running transaction or start a new one if no is running yet
  auto& transaction = GetTransaction();

  BlobLocation location(segment, cluster, blob);

  // First check whether we have already written this blob in the current transaction and return the written data if so
  // This will also ensure that we don't read an already deleted blob and throw an exception if so
  if (auto blobContent = transaction.ReadBlob(this, location)) {
    return *blobContent;
  }

  auto cachedBlob = cache->Get(location);

  if (cachedBlob && cachedBlob->transactionId == transaction.id) {
    // We already read this blob. Now if we also aready hold a compatible lock to the requested one, then we can simply return the cached blob
    auto currentLock = transaction.GetLockType(this, location);
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
  auto& network = session->Network();
  auto& client = network.Get(connectionId);
  auto request = network::message::BlobsRead::Create(id, 1, writeLock ? network::message::BlobsRead::LockMode::Write : network::message::BlobsRead::LockMode::Read);
  auto& address = *request->begin();
  address = location;
  address.ifCommitIdHigher = cachedBlob ? cachedBlob->lastUpdated : 0;
  client.SendMessageToServer(std::move(request));
  
  // Wait for the response and handle it
  auto response = network.ExpectMessage<network::message::BlobsReadResponse>(client);
  if (response->result == network::message::BlobsReadResponse::Result::SUCCESS) {
    if (cachedBlob && response->nBlobs == 0) {
      // Our cached blob is up to date, but we still successfully acquired the lock -> notify the transaction of the lock
      transaction.AcquiredLock(this, location, writeLock ? Transaction::LockMode::Write : Transaction::LockMode::Read);
      return cachedBlob->Data();
    } else if (response->nBlobs == 1) {
      // Server has responded with a newer version of the blob, or we don't have it in our cache yet
      auto& blobData = *response->begin();
      auto& cachedBlob = cache->Set(location, blobData.Data(), blobData.blobSize, blobData.commitId, transaction.id);
      transaction.AcquiredLock(this, location, writeLock ? Transaction::LockMode::Write : Transaction::LockMode::Read);
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

void Database::WriteBlob(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize) {
  if (segment > constants::MaxSegmentId) {
    throw Exception("Invalid segment id");
  }

  if (cluster > constants::MaxClusterId) {
    throw Exception("Invalid cluster id");
  }

  if (blob > constants::MaxBlobId) {
    throw Exception("Invalid blob id");
  }

  if (blobSize > constants::MaxBlobSize) {
    // Make sure, the client cannot write blobs, which are larger than the supported maximum
    throw exception::BlobTooLarge(blobSize);
  }

  auto sessionLock = session->Lock();
  WriteBlobInternal(segment, cluster, blob, blobData, blobSize);
}


void Database::WriteBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize) {
  assert(session->OwnsLock());

  // Get the currently running transaction or start a new one if no is running yet
  auto& transaction = GetTransaction();
  BlobLocation location(segment, cluster, blob);


  if (transaction.GetLockType(this, location) != Transaction::LockMode::Write) {
    // We don't have the write lock yet -> acquire it
    // Since we want to write the blob, we don't have to read the content, we just want the write lock
    WriteLockNoContent(location);
  }

  // Store the new blob data for the transaction commit in the transaction state.
  // The following call will throw an exception if we attempt to write a blob, which has been deleted in this transaction.
  transaction.WriteBlob(this, location, blobData, static_cast<blob_size>(blobSize));
}


blob_id Database::CreateBlob(segment_id segment, cluster_id cluster, const void* blobData, size_t blobSize) {
  // We perform this size check before actually creating the blob or else the exception is thrown after the blob 
  // has already been logically created, so we sadly have to perform this size check twice, but the alternative would be worse.
  if (blobSize > constants::MaxBlobSize) {
    throw exception::BlobTooLarge(blobSize);
  }

  if (segment > constants::MaxSegmentId) {
    throw Exception("Invalid segment id");
  }

  if (cluster > constants::MaxClusterId) {
    throw Exception("Invalid cluster id");
  }

  // Only one thread at a time may create a blob
  auto sessionLock = session->Lock();

  // Create the new blob and directly write the data into it
  auto blobId = CreateBlobInternal(segment, cluster);
  WriteBlobInternal(segment, cluster, blobId, blobData, blobSize);

  // Finally return the blob id to the caller so he knows, which blob has been created.
  return blobId;
}



cluster_id Database::CreateCluster(segment_id segment) {
  if (segment > constants::MaxSegmentId) {
    throw Exception("Invalid segment id");
  }

  // Only one thread at a time may create a cluster
  auto sessionLock = session->Lock();

  // Acquire a write lock to (NextFreeClusterId,NextFreeBlobId), which will allow us to create clusters and blobs cluster and
  // also tell us the next cluster id to use
  auto [data, size] = ReadBlobInternal(segment, constants::NextFreeClusterId, constants::NextFreeBlobId, true);
  assert(size == sizeof(cluster_id)); // This blob only consists of the cluster_id value

  cluster_id newClusterId = *static_cast<const cluster_id*>(data);
  if (newClusterId > constants::MaxClusterId) {
    // Segment is full, no more clusters can be created
    throw exception::ClusterLimitReached(segment);
  }

  // Update the NextFreeClusterId blob
  cluster_id nextFreeClusterId = newClusterId + 1;
  WriteBlobInternal(segment, constants::NextFreeClusterId, constants::NextFreeBlobId, &nextFreeClusterId, sizeof(nextFreeClusterId));

  // The cluster is now logically created, so we also implicitly hold a write lock to it
  auto transaction = Transaction::Get(session, connectionId); // ReadBlobInternal() has already started a transaction

  // Through creation we implicitly hold write locks to the cluster's special blobs
  transaction->AcquiredLock(this, BlobLocation(segment, newClusterId, constants::NextFreeBlobId), Transaction::LockMode::Write);
  transaction->AcquiredLock(this, BlobLocation(segment, newClusterId, constants::ClusterDeleteId), Transaction::LockMode::Write);

  // Write the nextFreeBlobId in the newly created cluster into our client cache to be able to create blobs in that cluster without 
  // querying the server (which doesn't know anything about this cluster anyway).
  blob_id nextFreeBlob = 0;
  WriteBlobInternal(segment, newClusterId, constants::NextFreeBlobId, &nextFreeBlob, sizeof(nextFreeBlob));


  // Create an empty blob 0 in the created cluster to allow the client to immediately read the content of that blob
  // after calling CreateCluster()
  CreateBlob(segment, newClusterId, nullptr, 0);
  

  // The cluster and the first blob are now logically created and the client holds write locks to both of them
  return newClusterId;
}


segment_id Database::CreateSegment() {
  // Only one thread at a time may create a segment
  auto sessionLock = session->Lock();

  // Acquire a write lock to (NextFreeClusterId,NextFreeBlobId), which will allow us to create clusters and blobs cluster and
  // also tell us the next cluster id to use
  auto [data, size] = ReadBlobInternal(constants::NextFreeSegmentId, constants::NextFreeClusterId, constants::NextFreeBlobId, true);
  assert(size == sizeof(segment_id)); // This blob only consists of the cluster_id value

  segment_id newSegmentId = *static_cast<const segment_id*>(data);
  if (newSegmentId > constants::MaxSegmentId) {
    // Segment is full, no more clusters can be created
    throw exception::SegmentLimitReached();
  }

  // Update the NextFreeSegmentId blob
  segment_id nextFreeSegmentId = newSegmentId + 1;
  WriteBlobInternal(constants::NextFreeSegmentId, constants::NextFreeClusterId, constants::NextFreeBlobId, &nextFreeSegmentId, sizeof(nextFreeSegmentId));


  // The segment is now logically created, so we also implicitly hold a write lock to it
  auto transaction = Transaction::Get(session, connectionId); // ReadBlobInternal() has already started a transaction

  // Through creation we implicitly hold write locks to the segment's special blobs
  transaction->AcquiredLock(this, BlobLocation(newSegmentId, constants::NextFreeClusterId, constants::NextFreeBlobId), Transaction::LockMode::Write);
  transaction->AcquiredLock(this, BlobLocation(newSegmentId, constants::SegmentDeleteId, constants::ClusterDeleteId), Transaction::LockMode::Write);

  // Write the next free cluster id for the newly created segment into our client cache to be able to create clusters in that segment
  // without querying the server (which doesn't know anything about this segment anyway).
  cluster_id nextFreeClusterId = 0;
  WriteBlobInternal(newSegmentId, constants::NextFreeClusterId, constants::NextFreeBlobId, &nextFreeClusterId, sizeof(nextFreeClusterId));


  // Create the 0 cluster and with it the 0 blob in this segment
  CreateCluster(newSegmentId);
  

  // The segment, the first cluster and the first blob are now logically created and the client holds write locks to both of them
  return newSegmentId;

}

void Database::DeleteBlob(segment_id segment, cluster_id cluster, blob_id blob) {
  auto sessionLock = session->Lock();

  // Get the currently running transaction or start a new one if no is running yet
  auto& transaction = GetTransaction();
  BlobLocation location(segment, cluster, blob);

  // Acquire a write lock for deletion of the blob if we don't already hold one.
  if (transaction.GetLockType(this, location) != Transaction::LockMode::Write) {
    WriteLockNoContent(location); // no need to fetch the blob content -> we delete the blob anyway
  }

  // Validate that the cluster isn't deleted yet and then mark the blob for deletion upon transaction commit
  transaction.DeleteBlob(this, location);

  // Remove the blob from our blob cache
  cache->RemoveBlob(location);
}


void Database::DeleteCluster(segment_id segment, cluster_id cluster) {
  auto sessionLock = session->Lock();

  // Get the currently running transaction or start a new one if no is running yet
  auto& transaction = GetTransaction();
  BlobLocation lockLocation(segment, cluster, constants::ClusterDeleteId);

  // Acquire a write lock for deletion of the cluster if we don't already hold one (which is unlikely)
  if (transaction.GetLockType(this, lockLocation) != Transaction::LockMode::Write) {
    WriteLockNoContent(lockLocation); // no need to fetch the blob content -> we only need to write into it
  }

  // Validate that the segment isn't deleted yet and then mark the cluster with all its segments
  // for deletion upon transaction commit
  transaction.DeleteCluster(this, segment, cluster);

  // Remove all cached blobs for that cluster
  cache->RemoveCluster(segment, cluster);
}


void Database::DeleteSegment(segment_id segment) {
  auto sessionLock = session->Lock();

  // Get the currently running transaction or start a new one if no is running yet
  auto& transaction = GetTransaction();
  BlobLocation lockLocation(segment, constants::SegmentDeleteId, constants::ClusterDeleteId);

  // Acquire a write lock for deletion of the segment if we don't already hold one (which is unlikely)
  if (transaction.GetLockType(this, lockLocation) != Transaction::LockMode::Write) {
    WriteLockNoContent(lockLocation); // no need to fetch the blob content -> we only need to write into it
  }

  // Mark the segment for deletion upon transaction commit
  transaction.DeleteSegment(this, segment);

  // Remove all cached blobs for that segment
  cache->RemoveSegment(segment);
}



void Database::Close() {
  auto sessionLock = session->Lock();

  if (Transaction::IsRunning(session)) {
    throw exception::DbCloseDuringTxn(name);
  }

  auto& network = session->Network();
  auto& client = network.Get(connectionId);
  client.SendMessageToServer(network::message::DatabaseClose::Create(id));
  auto message = network.AwaitMessage(client);
  if (auto closeResponse = message.Get<network::message::DatabaseCloseResponse>()) {
    if (closeResponse->result == network::message::DatabaseCloseResponse::Result::TRANSACTION_IN_PROGRESS) {
      assert(false); // Apparently the server believes that this client a transaction running, while the client thinks he doesn't
      throw exception::DbCloseDuringTxn(name);
    }

    if (closeResponse->result == network::message::DatabaseCloseResponse::Result::DATABASE_NOT_OPEN) {
      // This should also never happen unless the client somehow manages to call Close() twice without crashing 
      // after calling it on the already deleted Database object the second time.
      throw exception::DbNotOpen(name);
    }


    // Server confirmed closing of this database -> release connection and delete this database instance
    network.Release(connectionId);  
  } else if (auto closed = message.Get<network::message::ConnectionClosed>()) {
    // Server confirmed close of last database by simply closing the connection
    network.ServerClosedConnection(connectionId);
  } else {
    throw Exception("Unexpected server response to DatabaseClose");
  }

  // Important: We have to release the session lock BEFORE we delete this as this database may be the last object referencing the session,
  //            which would in turn delete the session state and with it the mutex.
  sessionLock.Unlock();

  delete this;
}


void Database::UpdateCacheForCommittedBlob(const BlobLocation& location, std::vector<uint8_t> data, commit_id commitId, uint64_t transactionId) {
  cache->Update(location, std::move(data), commitId, transactionId);
}

void Database::AssignStickyLocks(std::unique_ptr<internal::HeldLocks> stickyLocks) {
  // The following assertion should never trigger as receiving sticky locks means that the database accessed in the current transaction, but then
  // it should have transfered its sticky locks into the transaction.
  assert(!this->stickyLocks);
  this->stickyLocks = std::move(stickyLocks);
}


blob_id Database::CreateBlobInternal(segment_id segment, cluster_id cluster) {
  // Acquire a write lock on the NextFreeBlobId id, which will allow us to create blobs in this cluster and
  // also tell us the next blob id to use.
  auto [data, size] = ReadBlobInternal(segment, cluster, constants::NextFreeBlobId, true);
  assert(size == sizeof(blob_id)); // This blob only consists of the blob_id value

  BlobLocation newLocation(segment, cluster, *static_cast<const blob_id*>(data));
  if (newLocation.blob > constants::MaxBlobId) {
    // Cluster is full, no more blobs can be created
    throw exception::BlobLimitReached(segment, cluster);
  }

  // Update the NextFreeBlobId blob
  blob_id nextBlobId = newLocation.blob + 1;
  WriteBlobInternal(segment, cluster, constants::NextFreeBlobId, &nextBlobId, sizeof(blob_id));

  // The blob is now logically created, so we also implicitly hold a write lock to it
  auto transaction = Transaction::Get(session, connectionId); // ReadBlobInternal() has already started a transaction if not already
  transaction->AcquiredLock(this, newLocation, Transaction::LockMode::Write);

  // Now we logically created the blob and we implicitly created the write lock, now the caller just has to actually write some
  // blob data for this blob or else it won't actually be created on transaction commit.
  return newLocation.blob;
}


void Database::WriteLockNoContent(const BlobLocation& location) {
  auto& network = session->Network();

  // Create the request for this blob
  auto& transaction = GetTransaction();
  auto& client = network.Get(connectionId);
  auto request = network::message::BlobsRead::Create(id, 1, network::message::BlobsRead::LockMode::Delete);
  auto& address = *request->begin();
  address = location;

  // Send it and await the server's response
  client.SendMessageToServer(std::move(request));
  auto response = network.ExpectMessage<network::message::BlobsReadResponse>(client);
  
  // Handle response
  if (response->result == network::message::BlobsReadResponse::Result::SUCCESS) {
    assert(response->nBlobs == 0); // The server should never respond with a blob to a Delete request
    // Nothing to enter into the cache, but update the held lock type in the transaction
    transaction.AcquiredLock(this, location, Transaction::LockMode::Write);
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
      throw exception::BlobDoesNotExist();

    case network::message::BlobsReadResponse::Result::DATBASE_NOT_OPENED:
      // This should never happen unless the client lib doesn't track database open/closes correctly
      throw Exception("internal error: Database not opened!");

    case network::message::BlobsReadResponse::Result::NO_TRANSACTION_IN_PROGRESS:
      // This should never happen as the client lib will always start a transaction before requesting any blobs
      throw Exception("internal error: No transaction in progress!");

    case network::message::BlobsReadResponse::Result::LOCK_TIMEOUT:
      TODO("somehow add more detail to this error...");
      throw exception::LockTimeout();

    case network::message::BlobsReadResponse::Result::DEADLOCK:
      if (auto transaction = Transaction::Get(session, connectionId)) {
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



Transaction& Database::GetTransaction() {
  assert(session->OwnsLock());
  if (auto transaction = Transaction::Get(session, connectionId)) {
    if (stickyLocks) {
      // We already started a new transaction for an action in an other database, but we
      // didn't yet transfer the sticky locks associated with this database into the transaction
      // -> Do it now.
      ApplyStickyLocksToTansaction(*transaction);
    }


    // Transaction already in progress
    return *transaction;
  }

  // Start new transaction
  auto& network = session->Network();
  auto& client = network.Get(connectionId);
  client.SendMessageToServer(network::message::TransactionBegin::Create());
  
  // Wait for response from server
  auto response = network.ExpectMessage<network::message::TransactionBeginResponse>(client);
  
  if (!response->IsSuccess()) {
    switch (response->result) {
      case network::message::TransactionBeginResponse::Result::ERROR_ALREADY_IN_TRANSACTION:
        throw exception::TransactionAlreadyOpen();

      default:
        throw Exception("Unexpected TransactionBegin error response");
        assert(false); // unhandled error message
    }
  }



  // Now check all the locks to release (for all databases)
  auto& databases = session->Databases().openedDatabases;
  for (auto& dbLockEntry : *response) {
    auto pos = databases.find(dbLockEntry.databaseId);
    if (auto database = (pos != databases.end()) ? pos->second : nullptr) {
      if (database->stickyLocks) {
        database->stickyLocks->UpdateLocks(dbLockEntry.keep, dbLockEntry.begin(), dbLockEntry.end());
      }
    } else {
      // Should never happen... But if it does, how does the client recover from this? The easiest would be to just ignore it... right?
      throw Exception("internal error: Server responded with unknown database id in TransactionBeginResponse");
    }
  }

  // Finally create the transaction and transfer the sticky locks from this database (not all) into it
  // The other databases' sticky locks will be transferred the moment they are accessed the first time in this new transaction.
  auto& transaction = Transaction::Create(session, connectionId);
  if (stickyLocks) {
    // Transfer the sticky locks of this database into the transaction
    ApplyStickyLocksToTansaction(transaction);
  }
  return transaction;
}



void Database::ApplyStickyLocksToTansaction(Transaction& transaction) {
  if (stickyLocks) {
    cache->UpdateTxnIdForHeldLocks(*stickyLocks, transaction.id);
    transaction.UseStickyLocks(this, std::move(stickyLocks));
  }
}


}