#include "pch.hpp"
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
#include <blobs/Transaction.hpp>
#include <internal/Network.hpp>
#include <network/Client.hpp>

#include <network/message/All.hpp>
#include <common/BlobLocation.hpp>

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

  /** Store/Update blob data in the cache and return a reference to the stored blob
   */
  CachedBlob& Set(const BlobLocation& location, const void* data, blob_size size, commit_id lastUpdated, uint64_t transactionId) {
    auto& cachedBlob = cache[location];
    cachedBlob.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    cachedBlob.lastUpdated = lastUpdated;
    cachedBlob.transactionId = transactionId;
    return cachedBlob;
  }


private:
  std::unordered_map<BlobLocation, CachedBlob> cache;
};




Database::Database(std::string name, database_id id, connection_id connectionId) : name(std::move(name)), id(id), connectionId(connectionId), cache(new BlobCache) {}

Database::~Database() {
  delete cache;
}

Database* Database::Open(const char* connectionString) {
  // Get the connection to the database server (open or reuse)
  auto connectionId = internal::Network::Get(connectionString);

  FIXME("the database name is obviously not the full connection string, but for now we can treat it like this")
  std::string databaseName = connectionString;

  auto& client = internal::Network::Get(connectionId);
  client.SendDatabaseOpen(databaseName);

  // Await the DatabaseOpenResponse
  auto message = internal::Network::ExpectMessage<network::message::DatabaseOpenResponse>(client);
  
  if (message->result == network::message::DatabaseOpenResponse::Result::SUCCESS) {
    return new Database(databaseName, message->databaseId, connectionId);
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
  auto transaction = Transaction::Get(true);

  BlobLocation location(segment, cluster, blob);
  TODO("Fist check the transaction cache! Maybe we already called WriteBlob for that blob in that case we want to return the last written data!");


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
    TODO("If we only acquire a Delete lock or the blob didn't change since the last transaction, then the server will respond with fewer blobs than requested!");
    TODO("If the server responds with fever blobs than requested, we still have to register the acquired locks in the transaction")

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
  auto transaction = Transaction::Get(true);
  BlobLocation location(segment, cluster, blob);

  if (transaction->GetLockType(id, location) != Transaction::LockMode::Write) {
    // We don't have the write lock yet -> acquire it
    // Since we want to write the blob, we don't have to read the content, we just want the write lock
    WriteLockNoContent(location);
  }

  transaction->WriteBlob(id, location, blobData, static_cast<blob_size>(blobSize));
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







void Database::WriteLockNoContent(const BlobLocation& location) {
  // Create the request for this blob
  auto transaction = Transaction::Get(true);
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
      Transaction::AbortDeadlock();
      TODO("somehow add more detail to this error...")
        throw exception::Deadlock();

    default:
      throw Exception("Server sent unknown result code!");
  }
}



}