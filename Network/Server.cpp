#include <network/Network.hpp>

#include <iostream>

blobs::network::Server::Server(int listenPort) : listenPort(listenPort), listenSocket(INVALID_SOCKET) {
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
    if (listenSocket == INVALID_SOCKET) {
      throw blobs::network::exception("socket() failed with: ");
    }

    // Set the socket into dual stack mode
    DWORD ipv6OnlyValue = FALSE;
    if (auto error = setsockopt(listenSocket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&ipv6OnlyValue), sizeof(ipv6OnlyValue))) {
      throw blobs::network::exception("setsockopt() failed with: ");
    }

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

    if (bind(listenSocket, bindAddress->ai_addr, bindAddress->ai_addrlen)) {
      throw blobs::network::exception("bind() failed with: ");
    }
    freeaddrinfo(bindAddress);


    // Continue at: https://learn.microsoft.com/en-us/windows/win32/winsock/listening-on-a-socket

    // Start accepting connections
  } catch (std::exception& ex) {
    std::cerr << "FATAL: " << ex.what() << "\n";
  }
}

