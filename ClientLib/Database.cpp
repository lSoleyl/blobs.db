#include "pch.h"
#include <blobs/Database.hpp>
#include <network/Client.hpp>


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

  //TODO: For now lets establish the a test connection directly here
  network::Client client(serverAddress, port);

  //TODO: for testing purposes send 10 messages
  for (int i = 0; i < 10; ++i) {
    client.SendOpenDBMessage(connectionString);
  }

  //TODO: parse the connection string and fetch the server address from it
  //TODO: use a server manager to manage the server connection (only one connection per process!)
}
