#include <network/SocketServer.hpp>
#include <network/Network.hpp>
#include <network/message/All.hpp>

#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <cassert>

namespace blobs {

network::SocketServer::SocketServer(int listenPort) : listenPort(listenPort), running(true) {
  network::Initialize();
  networkThread = std::thread([this]() { ListenThreadMain(); });
}

network::SocketServer::~SocketServer() {
  // Simply schedule an IO completion packet, which will set the running state to false to exit the network thread.
  ioCompletionPort.PostSimpleTask([this]() { running = false; });

  // Wait for thread to exit
  networkThread.join();

  // We must first close all sockets and only then the completion port
  listenSocket.Reset();
  clients.Clear(); // reset all client connections
  ioCompletionPort.Reset();

  network::Shutdown();
}


network::MessagePointer network::SocketServer::AwaitMessage() {
  return receiveQueue.AwaitMessage();
}

void network::SocketServer::SendMessageToClient(client_id client, MessagePointer message) {
  clients.QueueClientMessage(client, std::move(message));
}

void network::SocketServer::ListenThreadMain() {
  SetThreadDescription(GetCurrentThread(), L"Server Network thread");

  try {

    // Create an IPv6 socket and set it into dual stack mode to accept both IPv6 and IPv4 connections
    listenSocket = CreateDualStackSocket();

    {
      auto listenAddress = GetListenAddress();
      if (bind(*listenSocket, listenAddress->ai_addr, static_cast<int>(listenAddress->ai_addrlen)) == SOCKET_ERROR) {
        throw network::exception("bind() failed with: ");
      }
    }

    if (listen(*listenSocket, SOMAXCONN) == SOCKET_ERROR) {
      throw network::exception("listen() failed with: ");
    }

    // Create the completion port to use for all async operations in this server thread
    ioCompletionPort.Create(); // or restrict to 1 thread here?


    // Now associate the listen socket with this completion port (we will actually associate all our sockets with this single completion port)
    ioCompletionPort.AssociateSocket(*listenSocket, this);

    // Start accepting new connections
    AcceptNewConnection();

    while (running) {
      // Main loop - just process IO completions (receiving and sending messages and processing the queue)
      ioCompletionPort.ProcessIOCompletionPacket();
    }

  } catch (std::exception& ex) {
    // Transmit this kind of error back to the server main thread
    receiveQueue.MessageReceived(network::message::NetworkException::Create(ex.what()));
  }
}


network::Resource<SOCKET> network::SocketServer::CreateDualStackSocket() const {
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


network::Resource<addrinfo*> network::SocketServer::GetListenAddress() const {
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

void network::SocketServer::AcceptNewConnection() {
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
      return;
    } else if (error == WSAECONNRESET) {
      // Client aborted the connection -> restart accepting
      return AcceptNewConnection();
    }

    throw network::exception("AcceptEx() failed with: ", error);
  }

  // Otherwise accept completed synchronously -> The IOCompletionPort will still be signalled
}

LPFN_GETACCEPTEXSOCKADDRS GetAcceptExSockaddrsFn = nullptr;


void network::SocketServer::HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) {
  auto& client = ProcessAcceptedConnection();
  // Notify the main thread about the new client connection.
  // We also include the remote ip in this message as requesting it would require additional synchronization
  receiveQueue.MessageReceived(message::ConnectionOpened::Create(client.id, client.remoteIp));
}

network::SocketServer::Client& network::SocketServer::ProcessAcceptedConnection() {
  auto& client = clients.Add(*this, std::move(accept.socket));

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


  // Accept another connection
  AcceptNewConnection();
  return client;
}

void network::SocketServer::ProcessReceivedMessage(Client& client, MessagePointer message) {
  TODO("Filter out illegal messages, which the client is not allowed to send over the network.");

  // Set the client id and put the message into the receive queue
  message->clientId = client.id;
  receiveQueue.MessageReceived(std::move(message));
}

network::SocketServer::Client::Client(SocketServer& server, client_id id, Resource<SOCKET>&& socket) :
  DuplexMessageSocket(std::move(socket), server.ioCompletionPort), server(server), id(id) {}



void network::SocketServer::Client::HandleSocketClosed() {
  // Post a connection closed message to notify the server main thread
  server.receiveQueue.MessageReceived(message::ConnectionClosed::Create(id));

  // Remove this client from the connection list, which will destroy this object
  server.clients.Remove(*this);
}

void network::SocketServer::Client::HandleMessageReceived(MessagePointer message) {
  server.ProcessReceivedMessage(*this, std::move(message));
}



network::SocketServer::Client& network::SocketServer::Clients::Add(SocketServer& server, Resource<SOCKET>&& socket) {
  TODO("If we have the maximum number of clients connected, we must simply reject new connections");
  assert(map.size() < std::numeric_limits<client_id>::max());

  // Find the next free id
  client_id id = ++lastId;
  while (map.find(id) != map.end()) {
    id = ++lastId;
  }

  // We only lock the modification down here, because we don't have to protect against concurrent calls of Add()/Remove() [only called from network thread]
  // we only have to protect against reads from another thread while we are modifying the data structures in Add()/Remove()
  std::unique_lock<std::mutex> lock(mutex);
  list.emplace_back(server, id, std::move(socket));
  auto& client = list.back();
  map.emplace(id, &client);

  return client;
}


void network::SocketServer::Clients::Remove(Client& client) {
  // Synchronize with reads from the main thread
  std::unique_lock<std::mutex> lock(mutex);
  map.erase(client.id);
  list.erase(std::find_if(list.begin(), list.end(), [&client](Client& listClient) { return &client == &listClient; }));
}

void network::SocketServer::Clients::Clear() {
  // Synchronize with reads from the main thread
  std::unique_lock<std::mutex> lock(mutex);
  list.clear();
  map.clear();
}


bool network::SocketServer::Clients::QueueClientMessage(client_id clientId, MessagePointer message) {
  // We must always acquire the mutex to ensure the list of clients isn't modified while we enqueue a message
  std::unique_lock<std::mutex> lock(mutex);
  auto pos = map.find(clientId);
  if (pos != map.end()) {
    pos->second->AccessSendQueue() << std::move(message);
    return true;
  } else {
    return false; // client doesn't exist
  }
}







}