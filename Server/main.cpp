#include "pch.hpp"

#include "Server.hpp"
#include "Client.hpp"

#include <iostream>

using namespace blobs;

int main() {

  std::cout << "Server initializing\n";

  //TODO: process outstanding transactions from the server log if any
  //TODO: to support this, the server log should be stored separately from the database
  server::Server server(8888);
  std::cout << "Server ready\n";

  server.ServerMain();

  std::cout << "Server exiting\n";
}