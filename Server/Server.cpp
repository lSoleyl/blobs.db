#include "pch.hpp"
#include "Server.hpp"
#include "Client.hpp"

#include <iostream>

namespace blobs {
namespace server {

Server::Server(int port) : server(port) {}


void Server::ServerMain() {
  TODO("Figure out when to shutdown the server");

  while (true) {
    auto message = server.AwaitMessage();
    LogMessage(*message);

    switch (message->type) {
      case network::message::Type::ConnectionOpened:
        HandleConnectionOpened(message.Cast<network::message::ConnectionOpened>());
        break;

      case network::message::Type::ConnectionClosed:
        HandleConnectionClosed(message.Cast<network::message::ConnectionClosed>());
        break;

      case network::message::Type::DatabaseOpen:
        HandleDatabaseOpen(message.Cast<network::message::DatabaseOpen>());
        break;

      case network::message::Type::DatabaseClose:
        HandleDatabaseClose(message.Cast<network::message::DatabaseClose>());
        break;

      case network::message::Type::BlobsRead:
        HandleBlobsRead(message.Cast<network::message::BlobsRead>());
        break;

      case network::message::Type::TransactionAbort:
        HandleTransactionAbort(message.Cast<network::message::TransactionAbort>());
        break;

        //TODO: handle other messages

      default:
        std::cerr << "[ERR] Server received message of unexpected type " << message->type << "(" << static_cast<int>(message->type) << ") from client " << message->clientId << "\n";
        break;
    }
  }
}



void Server::HandleConnectionOpened(network::MessagePointer_T<network::message::ConnectionOpened> message) {
  // New client connected (initialize logical client data)
  Client::ClientConnected(message->clientId);
}

void Server::HandleConnectionClosed(network::MessagePointer_T<network::message::ConnectionClosed> message) {
  // A client closed the connection
  TODO("Remove from client map, release all held locks, release database references and close database if this was the last one");
}

void Server::HandleDatabaseOpen(network::MessagePointer_T<network::message::DatabaseOpen> message) {
  // OpenDB request
  auto dbName = message->GetDatabaseName();

  TODO("Handle error in case database is not found");
  auto& db = server::Database::Get(dbName);
  auto& client = server::Client::Get(message->clientId);

  try {
    server.SendDatabaseOpenResponse(client.id, network::message::DatabaseOpenResponse::Result::SUCCESS, client.OpenDatabase(db));
  } catch (std::exception&) {
    server.SendDatabaseOpenResponse(client.id, network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN, 0);
  }
}


void Server::HandleDatabaseClose(network::MessagePointer_T<network::message::DatabaseClose> message) {
  TODO("Get the database to close");
  // Confirm closing the database to the client by replying with the same DatabaseClose message.
  // Here we simply post the same message object into the client send buffer to avoid the unnecessary reallocation
  auto clientId = message->clientId; // assign to local variable here, because we will move the message as a whole
  server.SendMessageToClient(clientId, std::move(message));

  TODO("Don't allow db close if there is a transaction running")
  TODO("If this was the last database, then we can also just close the connection")
}


void Server::HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message) {
  auto& client = server::Client::Get(message->clientId);
  auto database = client.GetDatabase(message->databaseId);
  if (!database) {
    server.SendMessageToClient(message->clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::DATBASE_NOT_OPENED));
    return;
  }

  TODO("Handle special blob ids (blobId = 0xFFFFFFFF -> cluster table)");

  if (message->nBlobsRequested == 1) { 
    // Fast path: at most 1 blob needs to be sent to the client
    auto& requestedBlob = *message->begin();
    auto blob = database->GetBlob(requestedBlob);
    if (!blob) {
      server.SendMessageToClient(message->clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST));
      return;
    }
    
    if (client.AcquireLocks(*message)) {
      // Locks successfully acquired (no conflicts) -> send response
      if (requestedBlob.ifCommitIdHigher >= blob->commitId) {
        // The client has the current version of the blob -> we can send an empty response
        server.SendMessageToClient(message->clientId, network::message::BlobsReadResponse::Create(0, 0));
      } else {
        // Client's blob is not up to date -> send the server's current version
        auto response = network::message::BlobsReadResponse::Create(blob->data.size());
        response->begin().SetBlob(requestedBlob, blob->commitId, blob->data.data(), static_cast<blob_size>(blob->data.size()));
        server.SendMessageToClient(message->clientId, std::move(response));
      }
    } else {
      // Queue this message to the databse to be processed as soon as the conflicting locks are released
      if (!database->QueueReadCheckDeadlock(std::move(message))) {
        // Deadlock detected -> cannot enqueue message
        client.AbortTransaction(); // abort the client's current transaction
        server.SendMessageToClient(client.id, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::DEADLOCK));
        TODO("Also check whether any outstanding reads can be completed now that the client has released his previously held locks");
      }
    }
  } else {
    TODO("Handle multi blob requests");
    assert(false);
  }
  
}


void Server::HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message) {
  AbortTransaction(message->clientId);
}



void Server::LogMessage(const network::message::Message& message) {
  FIXME("This should be disabled in production code as it probably slows down the server as the windows console is known to be slow");
  std::cout << "Client[" << message.clientId << "]: " << message << "\n";
}



void Server::AbortTransaction(client_id clientId) {
  auto& client = server::Client::Get(clientId);
  client.AbortTransaction();

  TODO("Now try to satisfy any outstanding reads in all databases opened by the client");

}



}}


