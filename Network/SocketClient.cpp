#include <network/SocketClient.hpp>
#include <network/Network.hpp>
#include <network/message/All.hpp>

#include <iostream>
#include <cassert>

namespace blobs {
  
network::SocketClient::SocketClient(std::string serverAddress, std::string serverPort) : serverAddress(serverAddress), serverPort(serverPort) {
  network::Initialize();

  // We must create the ioCompletionPort before actually starting the network thread as we may want to immediately
  // schedule messages to send and want to notify the thread about it using the completion port.
  ioCompletionPort.Create();

  // start the network thread
  networkThread = std::thread([this]() { NetworkThreadMain(); });
}


network::SocketClient::~SocketClient() {
  ioCompletionPort.StopProcessing();
  networkThread.join();

  // We must first close all sockets, then the completion port
  CloseSocket();
  ioCompletionPort.Reset();
  network::Shutdown();
}

void network::SocketClient::SendMessageToServer(MessagePointer&& message) {
  AccessSendQueue() << std::move(message);
}



network::MessagePointer network::SocketClient::AwaitMessage() {
  return receiveQueue.AwaitMessage();
}

network::Resource<addrinfo*> network::SocketClient::GetServerAddress() const {
  addrinfo* serverAddr = nullptr;
  addrinfo hints = { 0 };

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (auto error = getaddrinfo(serverAddress.c_str(), serverPort.c_str(), &hints, &serverAddr)) {
    throw network::exception("getaddrinfo() failed with: ", error);
  }

  return Resource<addrinfo*>(serverAddr);
}


network::Resource<SOCKET> network::SocketClient::ConnectToServer() const {
  // First translate the server address
  auto serverAddr = GetServerAddress();

  // Create the client socket
  Resource<SOCKET> socket(::socket(serverAddr->ai_family, serverAddr->ai_socktype, serverAddr->ai_protocol));
  if (!socket) {
    throw network::exception("Socket creation failed with: ");
  }

  if (connect(*socket, serverAddr->ai_addr, static_cast<int>(serverAddr->ai_addrlen)) == SOCKET_ERROR) {
    throw network::exception("connect() failed with: ");
  }

  // Disable the send buffer to be able to immediately messages from our buffers without the 
  // system buffering. This is supposed to reduce CPU usage and speed up transmission of our messages
  // see: https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/netds/winsock/iocp/server/IocpServer.Cpp
  int nZero = 0;
  if (setsockopt(*socket, SOL_SOCKET, SO_SNDBUF, (char*)&nZero, sizeof(nZero)) == SOCKET_ERROR) {
    throw network::exception("setsockopt(SO_SNDBUF=0) failed with: ");
  }

  return socket;
}

void network::SocketClient::NetworkThreadMain() {
  SetThreadDescription(GetCurrentThread(), L"Client Network thread");

  try {
    InitializeMessageSocket(ConnectToServer(), ioCompletionPort);

    // This loop may be terminated by calling ioCompletionPort.StopProcessing()
    while (true) { 
      ioCompletionPort.ProcessIOCompletionPacket();
    }


  } catch (IOCompletionPort::Stopped&) {
    // Processing stopped as requested by the main thread

  } catch (std::exception& ex) {
    // Transmit the exception to the main thread
    receiveQueue.MessageReceived(message::NetworkException::Create(ex.what()));
  }
}




void network::SocketClient::HandleSocketClosed() {
  // Notify the client's main thread about this by simply posting a connection closed message into the message receive queue
  receiveQueue.MessageReceived(message::ConnectionClosed::Create());
}

void network::SocketClient::HandleMessageReceived(MessagePointer message) {
  // Simply put the message into the client's receive queue for the main thread to handle it
  receiveQueue.MessageReceived(std::move(message));
}


}
