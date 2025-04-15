#include "pch.hpp"
#include <blobs/Database.hpp>
#include <blobs/Exception.hpp>
#include <internal/Network.hpp>
#include <network/Client.hpp>

#include <network/message/All.hpp>


blobs::Database::Database(std::string name, database_id id, connection_id connectionId) : name(std::move(name)), id(id), connectionId(connectionId) {}

blobs::Database* blobs::Database::Open(const char* connectionString) {
  // Get the connection to the database server (open or reuse)
  auto connectionId = internal::Network::Get(connectionString);

  // TODO: the database name is obviously not the full connection string, but for now we can treat it like this
  std::string databaseName = connectionString;

  auto& client = internal::Network::Get(connectionId);
  client.SendDatabaseOpen(databaseName);

  // Await the DatabaseOpenResponse
  auto message = internal::Network::AwaitMessage(client);
  if (auto rep = message.Get<network::message::DatabaseOpenResponse>()) {
    if (rep->result == network::message::DatabaseOpenResponse::Result::SUCCESS) {
      return new Database(databaseName, rep->databaseId, connectionId);
    } else if (rep->result == network::message::DatabaseOpenResponse::Result::DATABASE_NOT_FOUND) {
      throw blobs::Exception("Database not found!");
    } else if (rep->result == network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN) {
      throw blobs::Exception("Too many databases already open. Close unused databases and retry.");
    }
  } else if (auto closed = message.Get<network::message::ConnectionClosed>()) {
    throw blobs::Exception("Connection has been closed by the server");
  } else {
    throw blobs::Exception("Unexpected message received from the server");
  }

  // not reached
  return nullptr;
}



void blobs::Database::Close() {
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
    //TODO: the server should probably close the connection if we close the last database
    throw Exception("Unexpected server response to DatabaseClose");
  }

  delete this;
}