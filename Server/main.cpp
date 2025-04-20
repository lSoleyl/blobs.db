#include "pch.hpp"

#include "Server.hpp"
#include "Client.hpp"

#include <iostream>

using namespace blobs;

int main() {

  std::cout << "Server initializing\n";

  TODO("process outstanding transactions from the server log if any"); 
  // But to do this, the server log would need to be stored separately from the databse
  // Alternatively process the transaction log when opening a database for the first time.

  server::Server server(8888);
  std::cout << "Server ready\n";

  server.ServerMain();

  std::cout << "Server exiting\n";
}