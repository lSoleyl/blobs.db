#include "pch.hpp"
#include "Server.hpp"
#include "Client.hpp"

#include <iostream>

namespace blobs {
namespace server {

Server::Server(int port) : server(port) {}


void Server::ServerMain() {

  //TODO: when to shutdown the server?
  while (true) {
    auto message = server.AwaitMessage();
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
        std::cerr << "[ERR] Server received message of unexpected type " << static_cast<int>(message->type) << " from client " << message->clientId << "\n";
        break;
    }
  }
}

void Server::HandleConnectionOpened(network::MessagePointer_T<network::message::ConnectionOpened> message) {
  // New client connected (initialize logical client data)
  Client::ClientConnected(message->clientId);

  // For now simply log this
  std::cout << "Client[" << message->clientId << "] connected from " << message->GetRemoteIp() << "\n";
}

void Server::HandleConnectionClosed(network::MessagePointer_T<network::message::ConnectionClosed> message) {
  // A client closed the connection

  //TODO: Remove from client map, release all held locks, release database references and close database if this was the last one

  std::cout << "Client[" << message->clientId << "] disconnected\n";
}

void Server::HandleDatabaseOpen(network::MessagePointer_T<network::message::DatabaseOpen> message) {
  // OpenDB request
  auto dbName = message->GetDatabaseName();

  // log the message for now
  std::cout << "Client[" << message->clientId << "]: DatabaseOpen(" << dbName << ")\n";

  //TODO: handle error in case database is not found
  auto& db = server::Database::Get(dbName);
  auto& client = server::Client::Get(message->clientId);

  try {
    server.SendDatabaseOpenResponse(client.id, network::message::DatabaseOpenResponse::Result::SUCCESS, client.OpenDatabase(db));
  } catch (std::exception&) {
    server.SendDatabaseOpenResponse(client.id, network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN, 0);
  }
}


void Server::HandleDatabaseClose(network::MessagePointer_T<network::message::DatabaseClose> message) {
  //TODO: get the database to close
  std::cout << "Client[" << message->clientId << "]: DatabaseClose(" << static_cast<int>(message->databaseId) << ")\n";
  // Confirm closing the database to the client by replying with the same DatabaseClose message.
  // Here we simply post the same message object into the client send buffer to avoid the unnecessary reallocation
  auto clientId = message->clientId; // assign to local variable here, because we will move the message as a whole
  server.SendMessageToClient(clientId, std::move(message));

  //TODO: don't allow db close if there is a transaction running
  //TODO: if this was the last database, then we can also just close the connection
}


void Server::HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message) {
  //TODO: log the message

  auto& client = server::Client::Get(message->clientId);
  auto database = client.GetDatabase(message->databaseId);
  if (!database) {
    server.SendMessageToClient(message->clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::DATBASE_NOT_OPENED));
    return;
  }

  //TODO: Implicitly start a transaction (unless already in a transaction)
  //TODO: check and set locks (what if a lock conflicts with the request? queue it somehow?)
  //       -> We can simply queue this message and whenever locks are released we will check the queue for messages, which can be served now
  //TODO: check blob transaction id
 

  //TODO: handle special blob ids (blobId = 0xFFFFFFFF -> cluster table)

  if (message->nBlobsRequested == 1) { 
    // Fast path: at most 1 blob needs to be sent to the client
    auto& requestedBlob = *message->begin();
    auto blob = database->GetBlob(requestedBlob);
    if (!blob) {
      server.SendMessageToClient(message->clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST));
      return;
    }

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

    //TODO: handle multi blob requests

  }
  
}


void Server::HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message) {
  //TODO: simply release all locks, which are held by this client and cancel the current transaction (we don't need to send any confirmation for this message)
  //TODO: we should also make sure to clear any queued up locks for this client
}



}}


