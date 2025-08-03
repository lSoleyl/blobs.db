#pragma once

#include "ClientInterface.hpp"
#include "ServerInterface.hpp"


namespace blobs::network {

class IOCPReceiveMessageQueue;

/** This factory is used by server and client to create the actual instances of network::ServerInterface and network::ClientInterface.
 *  Before it can be used the caller must configure the factory to use once by either calling SocketFactory::Use() or StandaloneFactory::Use().
 */
class Factory {
public:
  virtual std::unique_ptr<ClientInterface> CreateClient(std::string serverAddress, int serverPort = 8108) = 0;

  /** Creates a server instance with the specified receive message queue
   */
  virtual std::unique_ptr<ServerInterface> CreateServer(IOCPReceiveMessageQueue& serverReceiveQueue, int listenPort = 8108) = 0;


  /** Get access to the currently configured client and server factory
   */
  static Factory& Instance();


protected:
  static void SetInstance(Factory* factory);

private:
  static Factory* instance;
};




}
