#include <network/Network.hpp>
#include <network/message/OpenDB.hpp>
#include <network/message/ConnectionClosed.hpp>

#include <iostream>
#include <cassert>

namespace blobs {
  
network::Client::Client(std::string serverAddress, std::string serverPort) : serverAddress(serverAddress), serverPort(serverPort) {
  network::Initialize();

  // We must create the ioCompletionPort before actually starting the network thread as we may want to immediately
  // schedule messages to send and want to notify the thread about it using the completion port.
  ioCompletionPort.Create();

  // start the network thread
  networkThread = std::thread([this]() { NetworkThreadMain(); });
}


network::Client::~Client() {
  //TODO: signal the network thread to exit immediately (through IOCompletionPort?)
  networkThread.join();

  // We must first close all sockets, then the completion port
  CloseSocket();
  ioCompletionPort.Reset();
  network::Shutdown();
}



void network::Client::SendOpenDBMessage(std::string_view databaseName) {
  // Acquire the access to the send queue and encode the message
  AccessSendQueue() << message::OpenDB::Create(databaseName);

  // The network thread is automatically notified upon the access token falling out of scope (if necessary)
}



network::MessagePointer network::Client::AwaitMessage() {
  return receiveQueue.AwaitMessage();
}

network::Resource<addrinfo*> network::Client::GetServerAddress() const {
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


network::Resource<SOCKET> network::Client::ConnectToServer() const {
  // First translate the server address
  auto serverAddr = GetServerAddress();

  // Create the client socket
  Resource<SOCKET> socket(::socket(serverAddr->ai_family, serverAddr->ai_socktype, serverAddr->ai_protocol));
  if (!socket) {
    throw network::exception("Socket creation failed with: ");
  }

  if (connect(*socket, serverAddr->ai_addr, serverAddr->ai_addrlen) == SOCKET_ERROR) {
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

void network::Client::NetworkThreadMain() {

  try {
    InitializeMessageSocket(ConnectToServer(), ioCompletionPort);

    //TODO: listen to some event, which will terminate the client and close the connection properly
    while (true) {
      ioCompletionPort.ProcessIOCompletionPacket();
    }


    CloseSocket();

  } catch (std::exception& ex) {
    //TODO: we should push the error just like any other message to the calling thread
    std::cerr << "FATAL: " << ex.what();
  }
}




void network::Client::HandleSocketClosed() {
  // Notify the client's main thread about this by simply posting a connection closed message into the message receive queue
  receiveQueue.MessageReceived(message::ConnectionClosed::Create());
}

void network::Client::HandleMessageReceived(MessagePointer message) {
  // Simply put the message into the client's receive queue for the main thread to handle it
  receiveQueue.MessageReceived(std::move(message));
}


}
