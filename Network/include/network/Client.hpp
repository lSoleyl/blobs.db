#pragma once

#include "DuplexMessageSocket.hpp"
#include "..\win_include.hpp"
#include "Resource.hpp"
#include <thread>

namespace blobs {
namespace network {

class Client final : public DuplexMessageSocket {
public:
  Client(std::string serverAddress, std::string serverPort);
  ~Client();

  void SendOpenDBMessage(std::string_view databaseName);

private:
  void NetworkThreadMain();

  Resource<SOCKET> ConnectToServer() const;

  Resource<addrinfo*> GetServerAddress() const; 

  /** Handle closed connection
   */
  virtual void HandleSocketClosed() override;

  /** Handle a new message being fully received
   */
  virtual void HandleMessageReceived(std::unique_ptr<message::Message> message) override;


  std::thread networkThread;
  std::string serverAddress;
  std::string serverPort;
  Resource<HANDLE> ioCompletionPort;
};


}
}