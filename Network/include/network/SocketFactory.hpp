#pragma once

#include "Factory.hpp"

namespace blobs::network {

class SocketFactory : private Factory {
public:
  /** Call this method to use the socket factory (network enabled factory)
   */
  static void Use();

private:
  SocketFactory();
  virtual std::unique_ptr<ClientInterface> CreateClient(std::string serverAddress, int serverPort = 8108) override;
  virtual std::unique_ptr<ServerInterface> CreateServer(int listenPort = 8108) override;
};





}
