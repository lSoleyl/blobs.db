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

  //TODO: if this was the last database, then we can also just close the connection
}


void Server::HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message) {
  //TODO: Implicitly start a transaction (unless already in a transaction)
  //TODO: check and set locks (what if a lock conflicts with the request? queue it somehow?)
  //       -> We can simply queue this message and whenever locks are released we will check the queue for messages, which can be served now
  //TODO: check blob transaction id
  //TODO: return the blobs which are newer
  //TODO: what about not existing blobs?
}


void Server::HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message) {
  //TODO: simply release all locks, which are held by this client and cancel the current transaction (we don't need to send any confirmation for this message)
  //TODO: we should also make sure to clear any queued up locks for this client
}



}}


