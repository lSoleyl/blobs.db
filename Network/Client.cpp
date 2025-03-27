#include <network/Network.hpp>

#include <iostream>

namespace blobs {
  
network::Client::Client(std::string serverAddress, std::string serverPort) : serverAddress(serverAddress), serverPort(serverPort) {
  network::Initialize();

  // start the network thread
  networkThread = std::thread([this]() { NetworkThreadMain(); });
}


network::Client::~Client() {
  //TODO: signal the network thread to exit immediately (through IOCompletionPort?)
  networkThread.join();

  // We must first close all sockets, then the completion port
  socket.Reset(); 
  ioCompletionPort.Reset();
  network::Shutdown();
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


void network::Client::ConnectToServer() {
  // First translate the server address
  auto serverAddr = GetServerAddress();

  // Create the client socket
  socket = ::socket(serverAddr->ai_family, serverAddr->ai_socktype, serverAddr->ai_protocol);
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
}

void network::Client::NetworkThreadMain() {

  try {
    // Create the completion port to use for all async operations in this server thread
    if (auto completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) {
      // We assign after the check, because this function returns NULL instead of INVALID_HANDLE_VALUE upon failure and I don't want to extend
      // Resource<HANDLE> to check for NULL too
      ioCompletionPort = completionPort;
    } else {
      throw network::exception("CreateIoCompletionPort() failed with: ", GetLastError());
    }

    ConnectToServer();

    if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(*socket), *ioCompletionPort, 0, 0)) {
      throw network::exception("Failed to associate the socket with the io completion port: ");
    }

  

    //TODO: listen to some event, which will terminate the client and close the connection properly
    while (true) {


      //TODO: send / receive data using WSARecv/WSASend and the IO completion port
    
      //TODO: for now we will just send data without any overlapped io
      const char* message = "test";
      if (send(*socket, message, strlen(message), 0) == SOCKET_ERROR) {
        throw network::exception("send() failed with: ");
      }

      //TODO: for now just send 20 messages
      static int count = 0;
      if (++count == 20) {
        break;
      }
      // Give the network some time for transmission or else the socket will be shutdown before the client can transmit all of the messages
      Sleep(1);
    }

    if (shutdown(*socket, SD_SEND) == SOCKET_ERROR) {
      throw network::exception("shutdown() failed with: ");
    }

  } catch (std::exception& ex) {
    std::cerr << "FATAL: " << ex.what();
  }

}






}
