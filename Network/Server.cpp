#include <network/Network.hpp>
#include <network/message/OpenDB.hpp>

#include <iostream>
#include <algorithm>

namespace blobs {

network::Server::Server(int listenPort) : listenPort(listenPort), running(true) {
  network::Initialize();
  networkThread = std::thread([this]() { ListenThreadMain(); });
}

network::Server::~Server() {
  //TODO: signal listen thread to exit somehow
  
  // Wait for thread to exit
  networkThread.join();

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

      DWORD bytesTransferred;
      void* completionKey;
      OVERLAPPED* overlapped;
      GetQueuedCompletionStatus(*ioCompletionPort, &bytesTransferred, reinterpret_cast<ULONG_PTR*>(&completionKey), &overlapped, INFINITE);


      //TODO: Notify the (logical) server about new connections and about closed connections, because he needs to react to them

      if (completionKey == this) {
        // A new connection arrived at our listen socket
        auto& client = ProcessAcceptedConnection();
        std::cout << "Client " << client.id << " connected from: " << client.remoteIp << '\n';
      } else {
        // completion key must be a Client* and we received some data
        Client* client = static_cast<Client*>(completionKey);
        if (bytesTransferred) {
          client->ProcessReceivedData(bytesTransferred, overlapped);
        } else {
          // Completion with no bytes received means the connection has been closed
          // Remove the client from the connection list
          std::cout << "Client " << client->id << " disconnected\n";
          clients.erase(std::find_if(clients.begin(), clients.end(), [client](Client& c) { return &c == client; }));
        }
      }




      // We can also use PostQueuedCompletionStatus() for efficient inter-thread communication. (https://learn.microsoft.com/en-us/windows/win32/fileio/postqueuedcompletionstatus)
      // -> especially useful to notify the thread about a shut down or something like this.
      //
      // TODO: How will the thread be notified about new messages to send from the message queue? Probably simply be also posting a message to the IOCompletionPort. That port will
      //       therefore be the single synchronization point for that thread... But how to ensure that nobody writes into the message queue while the thread pops a message out?
      //       Either we use a lock (probably too expensive) or some lock free alternative, since this kind of congestion should happen rarely

    }

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

LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrsFn = nullptr;

network::Server::Client& network::Server::ProcessAcceptedConnection() {
  clients.emplace_back(*this, std::move(accept.socket));
  auto& client = clients.back();

  // Load the GetAcceptExSockaddrs function on first call
  if (!GetAcceptExSockaddrsFn) {
    GUID GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    DWORD dwBytes;

    auto result = WSAIoctl(*listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidGetAcceptExSockaddrs, sizeof(GuidGetAcceptExSockaddrs), 
                           &GetAcceptExSockaddrsFn, sizeof(GetAcceptExSockaddrsFn), &dwBytes, NULL, NULL);
    if (result == SOCKET_ERROR) {
      throw network::exception("Loading GetAcceptExSockaddrs function pointer through WSAIoctl failed with: ");
    }
  }

  // Retrieve the remote IP from the buffer we passed into AcceptEx()
  sockaddr* localAddr;
  sockaddr* remoteAddr;
  int localAddrLen;
  int remoteAddrLen;
  GetAcceptExSockaddrsFn(accept.buffer, 0, sizeof(sockaddr_in6) + 16, sizeof(sockaddr_in6) + 16, &localAddr, &localAddrLen, &remoteAddr, &remoteAddrLen);

  // And translate the IP address using WSAAddressToString
  char ipBuffer[128];
  DWORD ipBufferSize = sizeof(ipBuffer);
  if (WSAAddressToStringA(remoteAddr, remoteAddrLen, NULL, ipBuffer, &ipBufferSize) == SOCKET_ERROR) {
    throw network::exception("WSAAddressToStringA() failed with: ");
  }
  client.remoteIp = ipBuffer; // This will contain the IP addres AND the port


  // Associate the client socket with our io completion port
  if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(*client.socket), *ioCompletionPort, reinterpret_cast<ULONG_PTR>(&client), 0)) {
    throw network::exception("CreateIoCompletionPort()3 failed with: ", GetLastError());
  }

  // Start receiving data
  client.ReceiveData();

  // Accept another connection
  AcceptNewConnection();

  return client;
}

void network::Server::ProcessReceivedMessage(Client& client, std::unique_ptr<message::Message> message) {
  // For now simply print it to the stdout

  //TODO: post these messages into some message queue to be processed by the actual server main thread
  std::cout << "Client [" << client.id << "]: ";
  if (message->type == message::Type::OpenDB) {
    std::cout << "OpenDB(" << static_cast<message::OpenDB*>(message.get())->GetDatabaseName() << ")\n";
  } else {
    std::cout << "Unkown message type (" << static_cast<int>(message->type) << ")\n";
  }
}




uint32_t lastClientId = 0;

network::Server::Client::Client(Server& server, Resource<SOCKET>&& socket) : server(server), socket(std::move(socket)), id(++lastClientId) {}

void network::Server::Client::ReceiveData(size_t bufferOffset) {
  receive.bufferInfo.buf = receive.buffer + bufferOffset;
  receive.bufferInfo.len = sizeof(receive.buffer) - bufferOffset;

  receive.flags = 0;
  receive.overlapped = { 0 }; // re initialize the overlapped structure before reusing it
  auto result = WSARecv(*socket, &receive.bufferInfo, 1, nullptr, &receive.flags, &receive.overlapped, nullptr);
  if (result == SOCKET_ERROR) {
    auto errorCode = WSAGetLastError();
    if (errorCode == WSA_IO_PENDING) {
      // Recevie successfully scheduled
      return;
    }
    throw network::exception("WSARecv() failed with: ", errorCode);
  } else {
    // Otherwise Recv() completed synchronously (because the client is sending so much), which we will ignore here
    // because the completion port has still been signalled and we will get the message from the completion port.
    // I verified this behavior in a simple test with a too small receive buffer
  }
}

void network::Server::Client::ProcessReceivedData(DWORD bytesTransferred, OVERLAPPED* overlapped) {
  // Add the buffer offset in case the last receive operation was performed with an offset
  bytesTransferred += receive.bufferInfo.buf - receive.buffer;
  std::string_view dataToProcess(receive.buffer, bytesTransferred);


  size_t receiveOffset = 0;

  while (!dataToProcess.empty()) {
    if (receive.message) {
      // We already have a paritally transferred message
      // First bytes go into the partially transferred message
      auto bytesToCopy = std::min(dataToProcess.size(), receive.message->size - receive.writeOffset);
      std::copy_n(dataToProcess.data(), bytesToCopy, reinterpret_cast<char*>(receive.message.get()) + receive.writeOffset);
      receive.writeOffset += bytesToCopy;
      dataToProcess.remove_prefix(bytesToCopy);
      if (receive.writeOffset == receive.message->size) {
        // Message fully read -> process it
        server.ProcessReceivedMessage(*this, std::move(receive.message));
      }
    } else if (dataToProcess.size() >= sizeof(decltype(message::Message::size))) {
      // We need to read at least the size of the message to be able to allocate anything
      auto messageSize = *reinterpret_cast<const decltype(message::Message::size)*>(dataToProcess.data());
      // Allocating as char[] and deleting as Message may be UB, but has always worked with msvc as the underlying allocation/deallocation function is the same.
      receive.message.reset(reinterpret_cast<message::Message*>(new char[messageSize]));
      
      // We must write at least the message size, otherwise the code in the above if-branch won't be able to tell how many
      // bytes are left to copy
      receive.message->size = messageSize;
      receive.writeOffset = sizeof(messageSize);
      dataToProcess.remove_prefix(sizeof(messageSize));

      // The remaining message bytes be written in the next iteration
    } else {
      // Not enough bytes to allocate the Message structure -> we must wait for more data
      // Copy the bytes to the start of the buffer if they aren't already and start receiving after these bytes.
      receiveOffset = dataToProcess.size();
      if (dataToProcess.data() > receive.buffer) {
        std::copy(dataToProcess.begin(), dataToProcess.end(), receive.buffer);
      }
      dataToProcess = std::string_view(); // reset to empty string view to break the loop and continue with ReceiveData()
    }
  }


  // And listen for more data
  ReceiveData(receiveOffset);
}














}