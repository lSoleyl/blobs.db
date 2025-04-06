#include "pch.h"
#include <blobs/Database.hpp>
#include <network/Client.hpp>
#include <network/message/OpenDBResponse.hpp>

#include <iostream>

void blobs::Database::Open(const char* connectionString) {
  //TODO: add better parsing than this here... maybe use regex for it instead  
  std::string serverAddress = connectionString;
  std::string port;
  auto colonPos = serverAddress.find(':');

  if (colonPos != std::string::npos) {
    port = serverAddress.substr(colonPos + 1);
    serverAddress = serverAddress.substr(0, colonPos);
  } else {
    port = "8888"; // default port for now
  }

  //TODO: use a server manager to manage the server connection (only one connection per process!)
  //TODO: For now lets establish the a test connection directly here
  network::Client client(serverAddress, port);

  client.SendOpenDBMessage(connectionString);

  // Await the OpenDBResponse
  auto message = client.AwaitMessage();
  if (auto rep = message.Get<network::message::OpenDBResponse>()) {
    //TODO: actually initialize a local logical database structure
    if (rep->result == network::message::OpenDBResponse::Result::SUCCESS) {
      std::cout << "OpenDB(" << connectionString << ") -> dbId: " << static_cast<int>(rep->databaseId) << "\n";
    } else {
      //TODO: handle error
    }
  } else {
    //TODO: handle error (like a closed connection and so on)
  }

  
}
