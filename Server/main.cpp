#include <network/Network.hpp>


#include <iostream>


int main() {

  std::cout << "Server initializing\n";

  blobs::network::Server server(8888);

  std::cout << "Server exiting\n";
}