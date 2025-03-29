#pragma once

#include "..\win_include.hpp"
#include "Resource.hpp"
#include <thread>
#include <deque>
#include <vector>
#include <mutex>

namespace blobs {
namespace network {

class Client {
public:
  Client(std::string serverAddress, std::string serverPort);
  ~Client();

  void SendOpenDBMessage(std::string_view databaseName);

private:
  void NetworkThreadMain();

  void ConnectToServer();


  Resource<addrinfo*> GetServerAddress() const;

  /** Checks whether there are messages to transmit and if so, will schedule a Send() for these messages
   */
  void SendMessagesFromQueue();

  void SendCompleted(DWORD bytesTransferred);
  
  /** This method should be called by the public Send...Message() methods in case the send.queue was empty
   *  prior to that call to ensure that the network thread is notified about the requested send operation on its IO completion port.
   */
  void NotifyNewSendMesssages() const;

  struct Receive {
    char buffer[1024]; // The raw message will be written here
    WSAOVERLAPPED overlapped;
  };

  //TODO: I would like to implement some smart send buffer rotation to not constantly reallocate the std::vectors for sending messages
  struct Send {
    std::vector<WSABUF> buffers; // Send buffers of the current send operation. If this is not empty, then a WSASend() is currently in progress
    std::deque<std::vector<char>> queue; // TODO: what is the correct type here?
    WSAOVERLAPPED overlapped;
    std::mutex mutex; // synchronizing access to the sendQueue between the network thread and the main thread
  };

  std::thread networkThread;
  std::string serverAddress;
  std::string serverPort;
  Resource<SOCKET> socket;
  Resource<HANDLE> ioCompletionPort;

  Receive receive;
  Send send;
};


}
}