#include "pch.hpp"
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
#include <blobs/Transaction.hpp>
#include <internal/Network.hpp>
#include <internal/HeldLocks.hpp>
#include <internal/DatabasesState.hpp>
#include <internal/TransactionsState.hpp>
#include <network/ClientInterface.hpp>

#include <network/message/All.hpp>
#include <common/BlobLocation.hpp>
#include <common/Encoding.hpp>

#include <charconv>
#include <algorithm>

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
  session->Databases(connectionId).openedDatabases.emplace(id, this);
}

Database::~Database() {
  assert(!session->OwnsLock()); // because we unlock it right before deletion to avoid holding past the session's deletion (in case the database is the last object referencing the session)
  session->EraseDatabase(connectionId, id);
}


Database* Database::Open(const Session::Handle& session, const char* connectionStringBegin, size_t connectionStringLen, OpenMode openMode) {
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

  return Open(session, hostName, databaseName, openMode, port);
}


Database* Database::Open(const Session::Handle& session, const wchar_t* connectionString, size_t connectionStringLen, OpenMode openMode) {
  // Simply encode into UTF-8 and then call the regular version
  std::string u8ConnectionString = encoding::ToUTF8(std::wstring_view(connectionString, connectionStringLen));
  return Open(session, u8ConnectionString.data(), u8ConnectionString.length(), openMode);
}


Database* Database::Open(const Session::Handle& session, const char* hostNameData, size_t hostNameLen, const char* databaseNameData, size_t databaseNameLen, OpenMode openMode, int port) {
  // Ensure direct conversion is possible
  static_assert(static_cast<int>(OpenMode::CreateIfNotExist) == static_cast<int>(network::message::DatabaseOpen::OpenMode::CreateIfNotExist));
  static_assert(static_cast<int>(OpenMode::OpenFailIfNotExist) == static_cast<int>(network::message::DatabaseOpen::OpenMode::OpenFailIfNotExist));
  static_assert(static_cast<int>(OpenMode::CreateFailIfExist) == static_cast<int>(network::message::DatabaseOpen::OpenMode::CreateFailIfExist));
  static_assert(static_cast<int>(OpenMode::CreateAlways) == static_cast<int>(network::message::DatabaseOpen::OpenMode::CreateAlways));



  std::string_view hostName(hostNameData, hostNameLen);
  std::string_view databaseName(databaseNameData, databaseNameLen);

  auto sessionLock = session->Lock();
  auto& network = session->Network();
  // Get the connection to the database server (open or reuse)
  auto connectionId = network.Get(hostName, port);

  auto& client = network.Get(connectionId);
  client.SendMessageToServer(network::message::DatabaseOpen::Create(databaseName, static_cast<network::message::DatabaseOpen::OpenMode>(openMode)));

  // Await the DatabaseOpenResponse
  auto message = network.ExpectMessage<network::message::DatabaseOpenResponse>(client);
  
  if (message->result == network::message::DatabaseOpenResponse::Result::SUCCESS) {
    return new Database(session, std::string(databaseName), message->databaseId, connectionId);
  }
  
  // Opening the database failed for some reason -> release the network connection to not leak it since
  // no database has been created to hold on to it.
  network.Release(connectionId);

  switch (message->result) {
    case network::message::DatabaseOpenResponse::Result::DATABASE_OPEN_FAILED:
      TODO("Get more details for the failure");
      throw Exception("Failed to open database!");

    case network::message::DatabaseOpenResponse::Result::ILLEGAL_DATABASE_PATH:
      throw exception::IllegalDatabasePath(std::string(databaseName));

    case network::message::DatabaseOpenResponse::Result::DATABASE_ALREADY_OPEN:
      throw exception::DbAlreadyOpen(std::string(databaseName));

    case network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN:
      throw exception::TooManyDbsOpen();

    case network::message::DatabaseOpenResponse::Result::DATABASE_DOES_NOT_EXIST:
      throw exception::DbDoesNotExist(std::string(databaseName));

    case network::message::DatabaseOpenResponse::Result::DATABASE_ALREADY_EXISTS:
      throw exception::DbAlreadyExists(std::string(databaseName));

    case network::message::DatabaseOpenResponse::Result::CANNOT_OVERWRITE_OPEN_DATABASE:
      throw exception::OverwriteOpenedDatabase(std::string(databaseName));
  }
  
  assert(false); // unhandled result type
  // not reached
  return nullptr;
}

Database* Database::Open(const Session::Handle& session, const char* hostName, size_t hostNameLen, const wchar_t* databaseName, size_t databaseNameLen, OpenMode openMode, int port) {
  // Simply convert the UTF-16 database name into UTF-8 and call the regular Open() function
  auto u8DatabaseName = encoding::ToUTF8(std::wstring_view(databaseName, databaseNameLen));
  return Database::Open(session, hostName, hostNameLen, u8DatabaseName.data(), u8DatabaseName.length(), openMode, port);
}



bool Database::SetMVCC(bool enable) {
  return std::exchange(mvcc.setting, enable);
}

bool Database::IsMVCC() const {
  return mvcc.active;
}

bool Database::HasTransaction() const {
  auto sessionLock = session->Lock();
  return Transaction::Get(session, connectionId) != nullptr;
}


std::pair<const void*, blob_size> Database::ReadBlob(segment_id segment, cluster_id cluster, blob_id blob, Lock lock) {
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
  return ReadBlobInternal(segment, cluster, blob, lock);
}


std::pair<const void*, blob_size> Database::ReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob, Lock lock) {
  assert(session->OwnsLock());
  // Some of the below casts rely on these to enumerations having the same values for the same names
  static_assert(static_cast<int>(Lock::None) == static_cast<int>(network::message::BlobsRead::LockMode::None));
  static_assert(static_cast<int>(Lock::Read) == static_cast<int>(network::message::BlobsRead::LockMode::Read));
  static_assert(static_cast<int>(Lock::Write) == static_cast<int>(network::message::BlobsRead::LockMode::Write));
  static_assert(static_cast<int>(Lock::None) == static_cast<int>(Transaction::LockMode::None));
  static_assert(static_cast<int>(Lock::Read) == static_cast<int>(Transaction::LockMode::Read));
  static_assert(static_cast<int>(Lock::Write) == static_cast<int>(Transaction::LockMode::Write));

  if (lock == Lock::None) {
    // Dirty reads are much more simplfied - no running transaction needed, no cache to update
    return DirtyReadBlobInternal(segment, cluster, blob);
  }


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
    if (static_cast<int>(currentLock) >= static_cast<int>(lock)) {
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
  auto request = network::message::BlobsRead::Create(id, 1, static_cast<network::message::BlobsRead::LockMode>(lock));
  auto& address = *request->begin();
  address = location;
  address.ifCommitIdHigher = cachedBlob ? cachedBlob->lastUpdated : 0;
  client.SendMessageToServer(std::move(request));
  
  // Wait for the response and handle it
  auto response = network.ExpectMessage<network::message::BlobsReadResponse>(client);
  if (response->result == network::message::BlobsReadResponse::Result::SUCCESS) {
    if (cachedBlob && response->nBlobs == 0) {
      // Our cached blob is up to date, but we still successfully acquired the lock -> notify the transaction of the lock
      transaction.AcquiredLock(this, location, static_cast<Transaction::LockMode>(lock));
      return cachedBlob->Data();
    } else if (response->nBlobs == 1) {
      // Server has responded with a newer version of the blob, or we don't have it in our cache yet
      auto& blobData = *response->begin();
      auto& cachedBlob = cache->Set(location, blobData.Data(), blobData.blobSize, blobData.commitId, transaction.id);
      transaction.AcquiredLock(this, location, static_cast<Transaction::LockMode>(lock));
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



std::pair<const void*, blob_size> Database::DirtyReadBlobInternal(segment_id segment, cluster_id cluster, blob_id blob) {
  assert(session->OwnsLock());
  
  // Request the blob from the server
  auto& network = session->Network();
  auto& client = network.Get(connectionId);
  auto request = network::message::BlobsRead::Create(id, 1, network::message::BlobsRead::LockMode::None);
  auto& address = *request->begin();
  address.segment = segment;
  address.cluster = cluster;
  address.blob = blob;
  address.ifCommitIdHigher = 0;
  
  client.SendMessageToServer(std::move(request));

  // Wait for the response and handle it
  auto response = network.ExpectMessage<network::message::BlobsReadResponse>(client);
  if (response->result == network::message::BlobsReadResponse::Result::SUCCESS) {
    if (response->nBlobs == 1) {
      // Server has sent the requested blob -> copy it into the session's dirty read buffer and return a pointer into it
      auto& blobData = *response->begin();
      auto blobDataBegin = static_cast<const uint8_t*>(blobData.Data());
      auto blobDataEnd = blobDataBegin + blobData.blobSize;

      // Copy the blob's data into the dirty read buffer
      auto& cache = session->Databases(connectionId).dirtyReadBuffer;
      cache.resize(blobData.blobSize);
      std::copy(blobDataBegin, blobDataEnd, cache.data());

      // Return a pointer into the dirty read cache
      return std::pair<const void*, blob_size>(cache.data(), blobData.blobSize);
    } else {
      assert(false); // Server repsonsed with an illegal number of blobs!
    }
  } else {
    // Handle error response
    HandleReadBlobErrorResponse(*response);
  }

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




void Database::CreateBlobAt(segment_id segment, cluster_id cluster, blob_id blob, const void* blobData, size_t blobSize) {
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

  if (blob > constants::MaxBlobId) {
    throw Exception("Invalid blob id");
  }
  
  // Only one thread at a time may create a blob
  auto sessionLock = session->Lock();

  // Create the blob and write the data into it
  CreateBlobInternalAt(segment, cluster, blob);
  WriteBlobInternal(segment, cluster, blob, blobData, blobSize);
}





cluster_id Database::CreateCluster(segment_id segment) {
  if (segment > constants::MaxSegmentId) {
    throw Exception("Invalid segment id");
  }

  // Only one thread at a time may create a cluster
  auto sessionLock = session->Lock();

  // Acquire a write lock to (NextFreeClusterId,NextFreeBlobId), which will allow us to create clusters and blobs cluster and
  // also tell us the next cluster id to use
  auto [data, size] = ReadBlobInternal(segment, constants::NextFreeClusterId, constants::NextFreeBlobId, Lock::Write);
  assert(size == sizeof(cluster_id)); // This blob only consists of the cluster_id value

  auto transaction = Transaction::Get(session, connectionId); // ReadBlobInternal() has already started a transaction
  // By acquiring a write lock on NextFreeClusterId we implicitly acquire a lock on the segment's list of all clusters.
  // This implicit locking is performed by the server.
  transaction->AcquiredLock(this, BlobLocation(segment, constants::ClusterListId, constants::BlobListId), Transaction::LockMode::Write);

  cluster_id newClusterId = *static_cast<const cluster_id*>(data);
  if (newClusterId > constants::MaxClusterId) {
    // Segment is full, no more clusters can be created
    throw exception::ClusterLimitReached(segment);
  }

  // Update the NextFreeClusterId blob
  cluster_id nextFreeClusterId = newClusterId + 1;
  WriteBlobInternal(segment, constants::NextFreeClusterId, constants::NextFreeBlobId, &nextFreeClusterId, sizeof(nextFreeClusterId));

  // The cluster is now logically created, so we also implicitly hold a write lock to it

  // Through creation we implicitly hold write locks to the cluster's special blobs
  transaction->AcquiredLock(this, BlobLocation(segment, newClusterId, constants::NextFreeBlobId), Transaction::LockMode::Write);
  transaction->AcquiredLock(this, BlobLocation(segment, newClusterId, constants::ClusterDeleteId), Transaction::LockMode::Write);
  transaction->AcquiredLock(this, BlobLocation(segment, newClusterId, constants::BlobListId), Transaction::LockMode::Write);

  // Mark the cluster as created during this transaction
  transaction->CreateCluster(this, segment, newClusterId);

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
  auto [data, size] = ReadBlobInternal(constants::NextFreeSegmentId, constants::NextFreeClusterId, constants::NextFreeBlobId, Lock::Write);
  assert(size == sizeof(segment_id)); // This blob only consists of the cluster_id value

  auto transaction = Transaction::Get(session, connectionId); // ReadBlobInternal() has already started a transaction

  // By acquiring a write lock on NextFreeSegmentId we implicitly acquire a lock on the list of all segments
  // This implicit locking is performed by the server.
  transaction->AcquiredLock(this, BlobLocation(constants::SegmentListId, constants::ClusterListId, constants::BlobListId), Transaction::LockMode::Write);

  segment_id newSegmentId = *static_cast<const segment_id*>(data);
  if (newSegmentId > constants::MaxSegmentId) {
    // Segment is full, no more clusters can be created
    throw exception::SegmentLimitReached();
  }

  // Update the NextFreeSegmentId blob
  segment_id nextFreeSegmentId = newSegmentId + 1;
  WriteBlobInternal(constants::NextFreeSegmentId, constants::NextFreeClusterId, constants::NextFreeBlobId, &nextFreeSegmentId, sizeof(nextFreeSegmentId));


  // The segment is now logically created, so we also implicitly hold a write lock to it

  // Through creation we implicitly hold write locks to the segment's special blobs
  transaction->AcquiredLock(this, BlobLocation(newSegmentId, constants::NextFreeClusterId, constants::NextFreeBlobId), Transaction::LockMode::Write);
  transaction->AcquiredLock(this, BlobLocation(newSegmentId, constants::SegmentDeleteId, constants::ClusterDeleteId), Transaction::LockMode::Write);
  transaction->AcquiredLock(this, BlobLocation(newSegmentId, constants::ClusterListId, constants::BlobListId), Transaction::LockMode::Write); // list of clusters


  // Mark the segment as created during this transaction
  transaction->CreateSegment(this, newSegmentId);

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

  // We must acquire a write lock into the cluster's list of all blobs before we can delete a blob
  // This must be done explicitly as the server cannot know that we want to delete a blob when requesting the write lock, especially
  // if we first write into the blob and only later in the transaction delete it.
  if (transaction.GetLockType(this, BlobLocation(segment, cluster, constants::BlobListId)) != Transaction::LockMode::Write) {
    // FIXME once we support multi blob requests, we could send just one request for locking both blobs
    WriteLockNoContent(BlobLocation(segment, cluster, constants::BlobListId)); // we don't fetch the content of the blob, we are only interested in the lock
  }

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
    // With this lock the server will implicitly grant the client a write lock to the list of all clusters in the segment (because it is modified during this transaction)
    // as well as NextFreeBlobId and all blobs inside that cluster to ensure consistency. 
    transaction.AcquiredLock(this, BlobLocation(segment, constants::ClusterListId, constants::BlobListId), Transaction::LockMode::Write);

    // We DO NOT store any of the other locks (inside the cluster) in our transaction state however even though the server will hold
    // these locks for the client as sticky locks in case this transaction is aborted. 
    // This results in the client re-requesting a write lock on the NextFreeBlobId if we attempt to create a 
    // blob in this cluster in the next transaction, but we would have to re-request that blob anyway because the
    // server may have granted us the implicit write lock, but the client still doesn't know the contents of that blob!
    // So there is no performance to be gained from actually storing the lock in the client unless we know the blob's contents.
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
    // With this lock the server will implicitly grant the client a write lock to the list of all segments (because it is modified during this transaction)
    // as well as NextFreeClusterId and all blobs inside that segment to ensure consistency. 
    transaction.AcquiredLock(this, BlobLocation(constants::SegmentListId, constants::ClusterListId, constants::BlobListId), Transaction::LockMode::Write);
    
    // We DO NOT store any of the other locks (inside the segment) in our transaction state however even though the server will hold
    // these locks for the client as sticky locks in case this transaction is aborted. 
    // This results in the client re-requesting a write lock on the NextFreeClusterId if we attempt to create a 
    // cluster in this segment in the next transaction, but we would have to re-request that blob anyway because the
    // server may have granted us the implicit write lock, but the client still doesn't know the contents of that blob!
    // So there is no performance to be gained from actually storing the lock in the client unless we know the blob's contents.
  }

  // Mark the segment for deletion upon transaction commit
  transaction.DeleteSegment(this, segment);

  // Remove all cached blobs for that segment
  cache->RemoveSegment(segment);
}


Range<blob_id> Database::GetAllBlobs(segment_id segment, cluster_id cluster, Lock lock) {
  if (segment > constants::MaxSegmentId) {
    throw Exception("Invalid segment id");
  }

  if (cluster > constants::MaxClusterId) {
    throw Exception("Invalid cluster id");
  }

  // Only one thread at a time may query the blob list
  auto sessionLock = session->Lock();

  // Get the currently running transaction or start a new one if no is running yet
  // Unless we attempt to perform a dirty (lockless) read then we don't need a running transaction
  auto transaction = (lock == Lock::None) ? nullptr : &GetTransaction();

  std::vector<blob_id> ids;

  // Only try to read the blob list if we are either performing a read outside any transaction (no lock)
  // or we didn't create the cluster in the same transaction, because then the read would fail with ClusterDoesNotExist.
  if (!transaction || !transaction->IsCreatedCluster(this, segment, cluster)) {
    // Read the blob list from the server or transaction cache. This will also validate that both segment and cluster exist
    // The returned list of blobs will be in ascending id order.
    auto [data, size] = ReadBlobInternal(segment, cluster, constants::BlobListId, lock);
    using BlobRange = std::pair<blob_id, blob_id>;

    // Construct a vector from all the blob ranges
    for (auto it = static_cast<const BlobRange*>(data), end = it + (size / sizeof(BlobRange)); it != end; ++it) {
      auto [blobBegin, blobEnd] = *it;
      for (auto blobId = blobBegin; blobId != blobEnd; ++blobId) {
        ids.push_back(blobId);
      }
    }
  }

  // Merge with the changes from the currently active transaction (unless we are performing a dirty read)
  if (transaction) {
    transaction->MergeBlobIdList(this, segment, cluster, ids);
  }

  // Convert the vector into the result object
  Range<blob_id> resultRange(new blob_id[ids.size()], ids.size());
  std::copy(ids.begin(), ids.end(), resultRange.begin());
  return resultRange;
}

Range<cluster_id> Database::GetAllClusters(segment_id segment, Lock lock) {
  if (segment > constants::MaxSegmentId) {
    throw Exception("Invalid segment id");
  }

  // Only one thread at a time may query the blob list
  auto sessionLock = session->Lock();

  // Get the currently running transaction or start a new one if no is running yet
  // Unless we attempt to perform a dirty (lockless) read then we don't need a running transaction
  auto transaction = (lock == Lock::None) ? nullptr : &GetTransaction();

  std::vector<cluster_id> ids;

  // Only try to read the cluster list if we are either performing a read outside any transaction (no lock)
  // or we didn't create the segment in the same transaction, because then the read would fail with SegmentDoesNotExist.
  if (!transaction || !transaction->IsCreatedSegment(this, segment)) {
    // Read the blob list from the server or transaction cache. This will also validate that both segment and cluster exist
    // The returned list of blobs will be in ascending id order.
    auto [data, size] = ReadBlobInternal(segment, constants::ClusterListId, constants::BlobListId, lock);
    using ClusterRange = std::pair<cluster_id, cluster_id>;

    // Construct a vector from all the blob ranges
    for (auto it = static_cast<const ClusterRange*>(data), end = it + (size / sizeof(ClusterRange)); it != end; ++it) {
      auto [clusterBegin, clusterEnd] = *it;
      for (auto clusterId = clusterBegin; clusterId != clusterEnd; ++clusterId) {
        ids.push_back(clusterId);
      }
    }

  }

  // Merge with the changes from the currently active transaction (unless we are performing a dirty read)
  if (transaction) {
    transaction->MergeClusterIdList(this, segment, ids);
  }

  // Convert the vector into the result object
  Range<cluster_id> resultRange(new cluster_id[ids.size()], ids.size());
  std::copy(ids.begin(), ids.end(), resultRange.begin());
  return resultRange;
}


Range<segment_id> Database::GetAllSegments(Lock lock) {
  // Only one thread at a time may query the segment list
  auto sessionLock = session->Lock();

  // Get the currently running transaction or start a new one if no is running yet
  // Unless we attempt to perform a dirty (lockless) read then we don't need a running transaction
  auto transaction = (lock == Lock::None) ? nullptr : &GetTransaction();

  // Read the segmnt list from the server or transaction cache.
  // The returned list of segments will be in ascending id order.
  auto [data, size] = ReadBlobInternal(constants::SegmentListId, constants::ClusterListId, constants::BlobListId, lock);
  using SegmentRange = std::pair<segment_id, segment_id>;

  // Construct a vector from all the segment ranges
  std::vector<segment_id> ids;
  for (auto it = static_cast<const SegmentRange*>(data), end = it + (size / sizeof(SegmentRange)); it != end; ++it) {
    auto [segmentBegin, segmentEnd] = *it;
    for (auto segmentId = segmentBegin; segmentId != segmentEnd; ++segmentId) {
      ids.push_back(segmentId);
    }
  }

  // Merge with the changes from the currently active transaction (unless we are performing a dirty read)
  if (transaction) {
    transaction->MergeSegmentIdList(this, ids);
  }

  // Convert the vector into the result object
  Range<segment_id> resultRange(new segment_id[ids.size()], ids.size());
  std::copy(ids.begin(), ids.end(), resultRange.begin());
  return resultRange;
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

Session::Handle Database::GetSession() const {
  return session;
}

void Database::UpdateCacheForCommittedBlob(const BlobLocation& location, std::vector<uint8_t> data, commit_id commitId, uint64_t transactionId) {
  cache->Update(location, std::move(data), commitId, transactionId);
}

void Database::RemoveCachedBlob(const BlobLocation& location) {
  cache->RemoveBlob(location);
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
  auto [data, size] = ReadBlobInternal(segment, cluster, constants::NextFreeBlobId, Lock::Write);
  assert(size == sizeof(blob_id)); // This blob only consists of the blob_id value

  auto transaction = Transaction::Get(session, connectionId); // ReadBlobInternal() has already started a transaction if not already
  // When acquring the lock on NextFreeBlobId we are implicitly acquring also the lock on the cluster's list of all blobs
  // This implicit locking is performed by the server.
  transaction->AcquiredLock(this, BlobLocation(segment, cluster, constants::BlobListId), Transaction::LockMode::Write);

  BlobLocation newLocation(segment, cluster, *static_cast<const blob_id*>(data));
  if (newLocation.blob > constants::MaxBlobId) {
    // Cluster is full, no more blobs can be created
    throw exception::BlobLimitReached(segment, cluster);
  }

  // Update the NextFreeBlobId blob
  blob_id nextBlobId = newLocation.blob + 1;
  WriteBlobInternal(segment, cluster, constants::NextFreeBlobId, &nextBlobId, sizeof(blob_id));

  // The blob is now logically created, so we also implicitly hold a write lock to it
  transaction->AcquiredLock(this, newLocation, Transaction::LockMode::Write);

  // Mark the blob as created during this transaction
  transaction->CreateBlob(this, newLocation);

  // Now we logically created the blob and we implicitly created the write lock, now the caller just has to actually write some
  // blob data for this blob or else it won't actually be created on transaction commit.
  return newLocation.blob;
}

void Database::CreateBlobInternalAt(segment_id segment, cluster_id cluster, blob_id blob) {
  // Acquire a write lock on the NextFreeBlobId id, which will allow us to create blobs in this cluster.
  auto [data, size] = ReadBlobInternal(segment, cluster, constants::NextFreeBlobId, Lock::Write);
  assert(size == sizeof(blob_id)); // This blob only consists of the blob_id value

  auto transaction = Transaction::Get(session, connectionId); // ReadBlobInternal() has already started a transaction if not already
  // When acquring the lock on NextFreeBlobId we are implicitly acquring also the lock on the cluster's list of all blobs
  // This implicit locking is performed by the server.
  transaction->AcquiredLock(this, BlobLocation(segment, cluster, constants::BlobListId), Transaction::LockMode::Write);


  blob_id nextFreeBlobId = *static_cast<const blob_id*>(data);
  BlobLocation newLocation(segment, cluster, blob);
  if (blob >= nextFreeBlobId) {
    // We must update nextFreeBlobId as we just created a blob with an id equal or higher to it
    nextFreeBlobId = blob + 1;
  } else {
    // We are attempting to create a blob in a range where blobs already exist, so we must validate that it doesn't yet exist!
    // We perform this check on the client as this has the advantage of requiring only one server message per transaction.
    // Creating this range on each call to CreateBlobAt() may not be optimal and a certain overhead compared to the regular CreatBlob()
    auto allBlobs = GetAllBlobs(segment, cluster);
    
    // Perform binary search to speed up search across huge ranges
    auto pos = std::lower_bound(allBlobs.begin(), allBlobs.end(), blob);
    if (pos != allBlobs.end() && *pos == blob) {
      // This blob already exists
      throw exception::BlobAlreadyExists();
    }
  }

  // Always write back the NextFreeBlobId to the server even if it didn't change. This simplifies commit validation a lot.
  WriteBlobInternal(segment, cluster, constants::NextFreeBlobId, &nextFreeBlobId, sizeof(blob_id));

  // The blob is now logically created, so we also implicitly hold a write lock to it
  transaction->AcquiredLock(this, newLocation, Transaction::LockMode::Write);

  // Mark the blob as created during this transaction
  transaction->CreateBlob(this, newLocation);

  // Now we logically created the blob and we implicitly created the write lock, now the caller just has to actually write some
  // blob data for this blob or else it won't actually be created on transaction commit.
}


void Database::WriteLockNoContent(const BlobLocation& location) {
  auto& network = session->Network();

  // Create the request for this blob
  auto& transaction = GetTransaction();
  auto& client = network.Get(connectionId);
  auto request = network::message::BlobsRead::Create(id, 1, network::message::BlobsRead::LockMode::Delete);
  auto& address = *request->begin();
  address = location;
  address.ifCommitIdHigher = 0;

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

  switch (response.result) {
    case network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST:
      throw exception::BlobDoesNotExist();

    case network::message::BlobsReadResponse::Result::CLUSTER_DOES_NOT_EXIST:
      throw exception::ClusterDoesNotExist();

    case network::message::BlobsReadResponse::Result::SEGMENT_DOES_NOT_EXIST:
      throw exception::SegmentDoesNotExist();

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
      throw exception::Deadlock(response.GetErrorDetails());

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

  // Start new transaction (for that server connection)
  auto& network = session->Network();
  auto& client = network.Get(connectionId);
  client.SendMessageToServer(network::message::TransactionBegin::Create(session->Transactions().useStickyLocks));
  
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
  auto& databases = session->Databases(connectionId).openedDatabases;
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