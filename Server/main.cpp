#include <network/Network.hpp>


#include <iostream>


int main() {

  std::cout << "Server initializing\n";

  blobs::network::Server server(8888);

  //TODO: wait for server messages to arrive and process them

  std::cout << "Server exiting\n";
}