#include "pch.hpp"

#include <network/Network.hpp>
#include <network/message/OpenDB.hpp>
#include <network/message/ConnectionOpened.hpp>
#include <network/message/ConnectionClosed.hpp>

#include "Client.hpp"

#include <iostream>

using namespace blobs;

int main() {

  std::cout << "Server initializing\n";

  //TODO: process outstanding transactions from the server log if any
  //TODO: to support this, the server log should be stored separately from the database
  network::Server server(8888);
  std::cout << "Server ready\n";

  


  while (true) {
    auto rawMessage = server.AwaitMessage();

    if (auto connect = rawMessage.Get<network::message::ConnectionOpened>()) {
      // New client connected (initialize logical client data)
      server::Client::ClientConnected(connect->clientId);
      
      // For now simply log this
      std::cout << "Client[" << connect->clientId << "] connected from " << connect->GetRemoteIp() << "\n";

    } else if (auto openDb = rawMessage.Get<network::message::OpenDB>()) {
      // OpenDB request
      auto dbName = openDb->GetDatabaseName();

      // log the message for now
      std::cout << "Client[" << openDb->clientId << "]: OpenDB(" << dbName << ")\n";

      //TODO: handle error in case database is not found
      auto& db = server::Database::Get(dbName);
      auto& client = server::Client::Get(openDb->clientId);

      try {
        server.SendOpenDBResponse(client.id, network::message::OpenDBResponse::Result::SUCCESS, client.OpenDatabase(db));
      } catch (std::exception&) {
        server.SendOpenDBResponse(client.id, network::message::OpenDBResponse::Result::TOO_MANY_DATABASES_OPEN, 0);
      }
    } else if (auto closed = rawMessage.Get<network::message::ConnectionClosed>()) {
      // A client closed the connection

      //TODO: Remove from client map, release all held locks, release database references and close database if this was the last one

      std::cout << "Client[" << closed->clientId << "] disconnected\n";
    }
    //TODO: handle other requests



  }

  //TODO: manage database requests, manage locks, transactions and so on...


  std::cout << "Server exiting\n";
}