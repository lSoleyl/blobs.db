#pragma once

#include <network/Factory.hpp>

namespace blobs::network {

/** This network factory provides the standlone versions of client and server where both client and server run in the same process
 *  with no network communication involved. We can therefore avoid all the network communication overhead and instead use synchronized 
 *  data structures for communication.
 */
class StandaloneFactory : private Factory {
public:

  /** Call this method to use the standalone factory
   */
  static void Use();

private:
  StandaloneFactory();
  virtual std::unique_ptr<ClientInterface> CreateClient(std::string serverAddress, int serverPort = 8108) override;
  virtual std::unique_ptr<ServerInterface> CreateServer(int listenPort = 8108) override;


  class Client;
  class Server;

  Server* server;
};


}

