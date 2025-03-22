#include <network/Network.hpp>

#include <iostream>

blobs::network::Server::Server(int listenPort) : listenPort(listenPort) {
  blobs::network::Initialize();
  listenThread = std::thread([this]() { ListenThreadMain(); });
}

blobs::network::Server::~Server() {
  //TODO: signal listen thread to exit somehow
  
  // Wait for thread to exit
  listenThread.join();

  blobs::network::Shutdown();
}


void blobs::network::Server::ListenThreadMain() {
  try {

    // Create an IPv6 socket and set it into dual stack mode to accept both IPv6 and IPv4 connections
    listenSocket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (!listenSocket) {
      throw blobs::network::exception("socket() failed with: ");
    }

    // Set the socket into dual stack mode
    DWORD ipv6OnlyValue = FALSE;
    if (auto error = setsockopt(*listenSocket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&ipv6OnlyValue), sizeof(ipv6OnlyValue))) {
      throw blobs::network::exception("setsockopt() failed with: ");
    }

    {
      auto listenAddress = GetListenAddress();
      if (bind(*listenSocket, listenAddress->ai_addr, listenAddress->ai_addrlen) == SOCKET_ERROR) {
        throw blobs::network::exception("bind() failed with: ");
      }
    }



    


    // Continue at: https://learn.microsoft.com/en-us/windows/win32/winsock/listening-on-a-socket

    // Start accepting connections
    // TODO Pump messages from any socket into the message queue for the server and from the reply-queue into the corresponding socket
    // TODO How to handle disconnects?

  } catch (std::exception& ex) {
    std::cerr << "FATAL: " << ex.what() << "\n";
  }
}

blobs::network::Resource<addrinfo*> blobs::network::Server::GetListenAddress() const {
  // Now get the listen sockaddr
  addrinfo* bindAddress;
  addrinfo hints = { 0 };

  hints.ai_family = AF_INET6; // IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_protocol = IPPROTO_TCP; // TCP
  hints.ai_flags = AI_PASSIVE; // we want to listen on this address

  if (auto error = getaddrinfo(NULL, std::to_string(listenPort).c_str(), &hints, &bindAddress)) {
    throw blobs::network::exception("getaddrinfo() failed with: ", error);
  }

  return blobs::network::Resource<addrinfo*>(bindAddress);
}

