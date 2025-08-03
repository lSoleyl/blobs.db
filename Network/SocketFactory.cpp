
#include <network/SocketFactory.hpp>
#include <network/SocketClient.hpp>
#include <network/SocketServer.hpp>


namespace blobs::network {


SocketFactory::SocketFactory() {}

void SocketFactory::Use() {
  static SocketFactory socketFactoryInstance;
  SetInstance(&socketFactoryInstance);
}


std::unique_ptr<ClientInterface> SocketFactory::CreateClient(std::string serverAddress, int serverPort) {
  return std::make_unique<SocketClient>(std::move(serverAddress), std::to_string(serverPort));
}

std::unique_ptr<ServerInterface> SocketFactory::CreateServer(IOCPReceiveMessageQueue& serverReceiveQueue, int listenPort) {
  return std::make_unique<SocketServer>(serverReceiveQueue, listenPort);
}


}