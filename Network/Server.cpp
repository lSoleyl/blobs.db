#include <network/Network.hpp>

#include <iostream>

namespace blobs {

network::Server::Server(int listenPort) : listenPort(listenPort), running(true) {
  network::Initialize();
  listenThread = std::thread([this]() { ListenThreadMain(); });
}

network::Server::~Server() {
  //TODO: signal listen thread to exit somehow
  
  // Wait for thread to exit
  listenThread.join();

  // We must first close all sockets and only then the completion port
  listenSocket.Reset();
  clients.clear(); // reset all client connections
  ioCompletionPort.Reset();

  network::Shutdown();
}


void network::Server::ListenThreadMain() {
  SetThreadDescription(GetCurrentThread(), L"Network thread");

  try {

    // Create an IPv6 socket and set it into dual stack mode to accept both IPv6 and IPv4 connections
    listenSocket = CreateDualStackSocket();

    {
      auto listenAddress = GetListenAddress();
      if (bind(*listenSocket, listenAddress->ai_addr, listenAddress->ai_addrlen) == SOCKET_ERROR) {
        throw network::exception("bind() failed with: ");
      }
    }

    if (listen(*listenSocket, SOMAXCONN) == SOCKET_ERROR) {
      throw network::exception("listen() failed with: ");
    }
    
    // Create the completion port to use for all async operations in this server thread
    if (auto completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0 /* or restrict to 1 thread here? */)) {
      // We assign after the check, because this function returns NULL instead of INVALID_HANDLE_VALUE upon failure and I don't want to extend
      // Resource<HANDLE> to check for NULL too
      ioCompletionPort = completionPort;
    } else {
      throw network::exception("CreateIoCompletionPort() failed with: ", GetLastError());
    }

    // Now associate the listen socket with this completion port (we will actually associate all our sockets with this single completion port)
    if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(*listenSocket), *ioCompletionPort, reinterpret_cast<ULONG_PTR>(this), 0)) {
      throw network::exception("CreateIoCompletionPort()2 failed with: ", GetLastError());
    }

    //TODO: Should we actually process an immediately accepted connection here?
    bool initialAccepted = AcceptNewConnection();

    //TODO: using an atomic here is pretty primitive as it requires us to actively check whether we should shut down
    //      We should change this to a synchronization object, which can be signalled so we can await it together with waiting for new socket messages.
    //      That way we also avoid this active polling kind of processing
    while (running) {
      // Main loop:

      DWORD bytesReceived;
      void* completionKey;
      OVERLAPPED* overlapped;
      GetQueuedCompletionStatus(*ioCompletionPort, &bytesReceived, reinterpret_cast<ULONG_PTR*>(&completionKey), &overlapped, INFINITE);


      //TODO: Notify the (logical) server about new connections and about closed connections, because he needs to react to them

      if (completionKey == this) {
        // A new connection arrived at our listen socket
        ProcessAcceptedConnection();
      } else {
        // completion key must be a Client* and we received some data
        //TODO: We could also pass the number of bytes read into the process function. Then the client doen't need to call WSAGetOverlappedResult()
        Client* client = static_cast<Client*>(completionKey);
        if (!client->ProcessReceivedData()) {
          // connection has been closed by the remote host
          // remove the client from the connection list
          clients.erase(std::find_if(clients.begin(), clients.end(), [client](Client& c) { return &c == client; }));
        }
      }




      // We can also use PostQueuedCompletionStatus() for efficient inter-thread communication. (https://learn.microsoft.com/en-us/windows/win32/fileio/postqueuedcompletionstatus)
      // -> especially useful to notify the thread about a shut down or something like this.
      //
      // TODO: How will the thread be notified about new messages to send from the message queue? Probably simply be also posting a message to the IOCompletionPort. That port will
      //       therefore be the single synchronization point for that thread... But how to ensure that nobody writes into the message queue while the thread pops a message out?
      //       Either we use a lock (probably too expensive) or some lock free alternative, since this kind of congestion should happen rarely

      

      //TODO: Set send buffer for new connections to 0... This was shown somewhere


      // Accept new connections

      // Pump messages in both directions

      // Repeat
    }



    // Continue at: https://learn.microsoft.com/en-us/windows/win32/winsock/listening-on-a-socket

    // Start accepting connections
    // TODO Pump messages from any socket into the message queue for the server and from the reply-queue into the corresponding socket
    // TODO How to handle disconnects?

  } catch (std::exception& ex) {
    std::cerr << "FATAL: " << ex.what() << "\n";
  }
}


network::Resource<SOCKET> network::Server::CreateDualStackSocket() const {
  Resource<SOCKET> newSocket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
  if (!newSocket) {
    throw network::exception("socket() failed with: ");
  }

  // Set the socket into dual stack mode
  DWORD ipv6OnlyValue = FALSE;
  if (setsockopt(*newSocket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char*>(&ipv6OnlyValue), sizeof(ipv6OnlyValue)) == SOCKET_ERROR) {
    throw network::exception("setsockopt(IPV6_ONLY=FALSE) failed with: ");
  }

  // Disable the send buffer to be able to immediately messages from our buffers without the 
  // system buffering. This is supposed to reduce CPU usage and speed up transmission of our messages
  // see: https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/netds/winsock/iocp/server/IocpServer.Cpp
  int nZero = 0;
  if (setsockopt(*newSocket, SOL_SOCKET, SO_SNDBUF, (char*)&nZero, sizeof(nZero)) == SOCKET_ERROR) {
    throw network::exception("setsockopt(SO_SNDBUF=0) failed with: ");
  }

  return newSocket;
}


network::Resource<addrinfo*> network::Server::GetListenAddress() const {
  // Now get the listen sockaddr
  addrinfo* bindAddress;
  addrinfo hints = { 0 };

  hints.ai_family = AF_INET6; // IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_protocol = IPPROTO_TCP; // TCP
  hints.ai_flags = AI_PASSIVE; // we want to listen on this address

  if (auto error = getaddrinfo(nullptr, std::to_string(listenPort).c_str(), &hints, &bindAddress)) {
    throw network::exception("getaddrinfo() failed with: ", error);
  }

  return network::Resource<addrinfo*>(bindAddress);
}

LPFN_ACCEPTEX AcceptExFn = nullptr;

bool network::Server::AcceptNewConnection() {
  if (!AcceptExFn) {
    // Load the function pointer on the first call
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    DWORD dwBytes;

    auto result = WSAIoctl(*listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx, sizeof(GuidAcceptEx), &AcceptExFn, sizeof(AcceptExFn), &dwBytes, NULL, NULL);
    if (result == SOCKET_ERROR) {
      throw network::exception("Loading AcceptEx function pointer through WSAIoctl failed with: ");
    }
  }

  // Zero the OVERLAPPED structure before reuse
  accept.overlapped = { 0 };

  // Create a new accept socket for the next connection
  accept.socket = CreateDualStackSocket();
  DWORD bytesReceived;
  BOOL success = AcceptExFn(*listenSocket, *accept.socket, accept.buffer, 0, sizeof(sockaddr_in6)+16, sizeof(sockaddr_in6)+16, &bytesReceived, &accept.overlapped);
  if (!success) {
    auto error = WSAGetLastError();
    if (error == ERROR_IO_PENDING) {
      // No error -> the async operation has been scheduled
      return false;
    } else if (error == WSAECONNRESET) {
      // Client aborted the connection -> restart accepting
      return AcceptNewConnection();
    }

    throw network::exception("AcceptEx() failed with: ", error);
  }

  return true; // Accept completed synchronously
}


void network::Server::ProcessAcceptedConnection() {
  clients.emplace_back(std::move(accept.socket));
  auto& client = clients.back();

  // Associate the client socket with our io completion port
  if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(*client.socket), *ioCompletionPort, reinterpret_cast<ULONG_PTR>(&client), 0)) {
    throw network::exception("CreateIoCompletionPort()3 failed with: ", GetLastError());
  }

  // Start receiving data
  client.ReceiveData();

  // Accept another connection
  AcceptNewConnection();
}


network::Server::Client::Client(Resource<SOCKET>&& socket) : socket(std::move(socket)) {
  receiveBufferInfo.buf = receiveBuffer;
  receiveBufferInfo.len = sizeof(receiveBuffer);
}

void network::Server::Client::ReceiveData() {
  recvFlags = 0;
  receiveOverlapped = { 0 }; // re initialize the overlapped structure before reusing it
  auto result = WSARecv(*socket, &receiveBufferInfo, 1, nullptr, &recvFlags, &receiveOverlapped, nullptr);
  if (result == SOCKET_ERROR) {
    auto errorCode = WSAGetLastError();
    if (errorCode == WSA_IO_PENDING) {
      // Recevie successfully scheduled
      return;
    }
    throw network::exception("WSARecv() failed with: ", errorCode);
  } else {

    //TODO: what to do here? Call WSAGetOverlappedResult? How do I know the amount of bytes transferred?
    throw std::exception("WSARecv() completed synchronously!");
  }
}

bool network::Server::Client::ProcessReceivedData() {
  DWORD bytesTransferred = 0;
  if (WSAGetOverlappedResult(*socket, &receiveOverlapped, &bytesTransferred, FALSE, &recvFlags)) {
    // Receive operation completed -> for now simply print the received message
    std::cout << "RECV: [" << std::string(receiveBuffer, bytesTransferred) << "]\n";

    //TODO: Parse message header, wait for a complete mesage then put it into the server's message queue and notify the server about the new message

    // And listen for more data
    ReceiveData();
  } else {
    auto errorCode = WSAGetLastError();
    if (errorCode == WSA_IO_PENDING) {
      throw network::exception("Client::ProcessReceivedData() called even though the operation is still pending!", errorCode);
    }

    if (errorCode == WSAECONNRESET) {
      // Connection has been closed by the remote client
      return false; // the caller will remove this client from its list
    }

    throw network::exception("Client::ProcessReceivedData() : WSAGetOverlappedResult() failed with: ", errorCode);
  }

  return true;
}














}