#pragma once

#include "DuplexMessageSocket.hpp"
#include "ReceiveMessageQueue.hpp"
#include "..\win_include.hpp"
#include "Resource.hpp"
#include <thread>

namespace blobs {
namespace network {

class Client final : private DuplexMessageSocket {
public:
  Client(std::string serverAddress, std::string serverPort);
  ~Client();

  void SendDatabaseOpen(std::string_view databaseName);
  void SendDatabaseClose(database_id databaseId);

  /** Used to send already allocated messages to the server (SendMessage() is sadly already in use by WinAPI)
   */
  void SendMessageToServer(MessagePointer&& message);

  /** Wait for the next sever message without a timeout
   */
  MessagePointer AwaitMessage();


private:
  void NetworkThreadMain();

  Resource<SOCKET> ConnectToServer() const;

  Resource<addrinfo*> GetServerAddress() const;

  /** Handle closed connection
   */
  virtual void HandleSocketClosed() override;

  /** Handle a new message being fully received
   */
  virtual void HandleMessageReceived(MessagePointer message) override;


  std::thread networkThread;
  std::string serverAddress;
  std::string serverPort;
  IOCompletionPort ioCompletionPort;


  ReceiveMessageQueue receiveQueue;
};


}
}