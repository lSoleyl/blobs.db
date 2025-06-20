
#include <network/SocketFactory.hpp>
#include <network/Client.hpp>
#include <network/Server.hpp>


namespace blobs::network {


SocketFactory::SocketFactory() {}

void SocketFactory::Use() {
  static SocketFactory socketFactoryInstance;
  SetInstance(&socketFactoryInstance);
}


std::unique_ptr<ClientInterface> SocketFactory::CreateClient(std::string serverAddress, int serverPort) {
  return std::make_unique<Client>(std::move(serverAddress), std::to_string(serverPort));
}

std::unique_ptr<ServerInterface> SocketFactory::CreateServer(int listenPort) {
  return std::make_unique<Server>(listenPort);
}


}