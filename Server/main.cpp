#include <network/Network.hpp>
#include <network/message/OpenDB.hpp>
#include <network/message/ConnectionOpened.hpp>
#include <network/message/ConnectionClosed.hpp>

#include <iostream>


int main() {

  std::cout << "Server initializing\n";

  //TODO: process outstanding transactions from the server log if any
  //TODO: to support this, the server log should be stored separately from the database
  blobs::network::Server server(8888);
  std::cout << "Server ready\n";

  while (true) {
    auto rawMessage = server.AwaitMessage();

    if (auto connect = rawMessage.Get<blobs::network::message::ConnectionOpened>()) {
      // New client connected
      //TODO: initialize some client data
      // For now simply log this
      std::cout << "Client[" << connect->clientId << "] connected from " << connect->GetRemoteIp() << "\n";

    } else if (auto openDb = rawMessage.Get<blobs::network::message::OpenDB>()) {
      // OpenDB request
      auto dbName = openDb->GetDatabaseName();

      // log the message for now
      std::cout << "Client[" << openDb->clientId << "]: OpenDB(" << dbName << ")\n";

      //TODO: keep track of all open database for all clients
      server.SendOpenDBResponse(openDb->clientId, blobs::network::message::OpenDBResponse::Result::SUCCESS, 1);

    } else if (auto closed = rawMessage.Get<blobs::network::message::ConnectionClosed>()) {
      // A client closed the connection

      std::cout << "Client[" << closed->clientId << "] disconnected\n";
    }
    //TODO: handle other requests



  }

  //TODO: manage database requests, manage locks, transactions and so on...


  std::cout << "Server exiting\n";
}