#include "pch.hpp"
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
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
  CachedBlob& Set(const BlobLocation& location, const void* data, blob_size size, commit_id lastUpdated) {
    auto& cachedBlob = cache[location];
    cachedBlob.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    cachedBlob.lastUpdated = lastUpdated;
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
  TODO("add synchronization: Only one thread may communicate with the database at any given time.")

  BlobLocation location(segment, cluster, blob);
  auto cachedBlob = cache->Get(location);
  
  TODO("If we already requested this blob with the same lock inside the same transaction, then we can simply return it from our cache without talking to the server.")

  // Request the blob from the server
  auto& client = internal::Network::Get(connectionId);
  auto request = network::message::BlobsRead::Create(id, 1, writeLock);
  auto& address = *request->begin();
  address = location;
  address.ifCommitIdHigher = cachedBlob ? cachedBlob->lastUpdated : 0;
  client.SendMessageToServer(std::move(request));
  
  // Wait for the response and handle it
  auto response = internal::Network::ExpectMessage<network::message::BlobsReadResponse>(client);
  if (response->result == network::message::BlobsReadResponse::Result::SUCCESS) {
    if (cachedBlob && response->nBlobs == 0) {
      // Our cached blob is up to date
      return cachedBlob->Data();
    } else if (response->nBlobs == 1) {
      // Server has responded with a newer version of the blob, or we don't have it in our cache yet
      auto& blobData = *response->begin();
      auto& cachedBlob = cache->Set(location, blobData.Data(), blobData.blobSize, blobData.commitId);
      return cachedBlob.Data();
    } else {
      assert(false); // server repsonsed with an illegal number of blobs!
    }
  } else {
    // Error Response
    switch (response->result) {
      case network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST:
        throw Exception("Requested blob does not exist!");

      case network::message::BlobsReadResponse::Result::DATBASE_NOT_OPENED:
        // This should never happen unless the client lib doesn't track database open/closes correctly
        throw Exception("internal error: Database not opened!");

      case network::message::BlobsReadResponse::Result::LOCK_TIMEOUT:
        TODO("somehow add more detail to this response... or should we return a different message for this error?")
        throw Exception("Lock timeout while waiting to lock!");

      case network::message::BlobsReadResponse::Result::DEADLOCK:
        TODO("somehow add more detail to this response... or should we return a different message for this error?")
        throw Exception("Deadlock occurred while waiting for lock!");

      default:
        throw Exception("Server sent unknown result code!");
    }
  }

  // not reached
  return std::make_pair(nullptr, 0);
}



void Database::Close() {
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

}