#pragma once

#include "..\win_include.hpp"
#include "Resource.hpp"

#include <thread>

namespace blobs {
namespace network {

class Client {
public:
  Client(std::string serverAddress, std::string serverPort);
  ~Client();

private:
  void NetworkThreadMain();

  void ConnectToServer();


  Resource<addrinfo*> GetServerAddress() const;


  std::thread networkThread;
  std::string serverAddress;
  std::string serverPort;
  Resource<SOCKET> socket;
  Resource<HANDLE> ioCompletionPort;
};


}
}