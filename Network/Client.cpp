#include <network/Network.hpp>
#include <network/message/OpenDB.hpp>

#include <iostream>
#include <cassert>

namespace blobs {
  
network::Client::Client(std::string serverAddress, std::string serverPort) : serverAddress(serverAddress), serverPort(serverPort) {
  network::Initialize();

  // We must create the ioCompletionPort before actually starting the network thread as we may want to immediately
  // schedule messages to send and want to notify the thread about it using the completion port.
  if (auto completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) {
    // We assign after the check, because this function returns NULL instead of INVALID_HANDLE_VALUE upon failure and I don't want to extend
    // Resource<HANDLE> to check for NULL too
    ioCompletionPort = completionPort;
  } else {
    throw network::exception("CreateIoCompletionPort() failed with: ", GetLastError());
  }

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
  // Acquire the access to send queue and encode the message
  // For now we ignore the return value as we always start with an emtpy buffer
  message::OpenDB::EncodeMessage(*AccessSendQueue(), databaseName);
  
  // The network thread is automatically notified upon the access token falling out of scope (if necessary)
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
    InitializeMessageSocket(ConnectToServer(), *ioCompletionPort);

    //TODO: listen to some event, which will terminate the client and close the connection properly
    while (true) {

      DWORD bytesTransferred;
      IOCompletionHandler* completionHandler;
      OVERLAPPED* overlapped;
      GetQueuedCompletionStatus(*ioCompletionPort, &bytesTransferred, reinterpret_cast<ULONG_PTR*>(&completionHandler), &overlapped, INFINITE);
      completionHandler->HandleIOCompletion(bytesTransferred, overlapped);
    }


    CloseSocket();

  } catch (std::exception& ex) {
    //TODO: we should push the error just like any other message to the calling thread
    std::cerr << "FATAL: " << ex.what();
  }
}




void network::Client::HandleSocketClosed() {
  //TODO: what to do here? reconnect? Quit? Notify the main thread?
  std::cout << "Connection to server lost\n";
}

void network::Client::HandleMessageReceived(std::unique_ptr<message::Message> message) {
  //TODO: pass the message to the main client thread
  std::cout << "Server: ";

  if (message->type == message::Type::OpenDB) {
    std::cout << "OpenDB(" << static_cast<message::OpenDB*>(message.get())->GetDatabaseName() << ")\n";
  } else {
    std::cout << "Unkown message type (" << static_cast<int>(message->type) << ")\n";
  }
}


}
