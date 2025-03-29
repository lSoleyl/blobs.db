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
  socket.Reset(); 
  ioCompletionPort.Reset();
  network::Shutdown();
}



void network::Client::SendOpenDBMessage(std::string_view databaseName) {
  // Acquire the mutex for the send queue
  std::unique_lock<std::mutex> lock(send.mutex);
  bool wasEmpty = send.queue.empty();
  send.queue.push_back({});
  // for now we ignore the return value as we always start with an emtpy buffer
  message::OpenDB::EncodeMessage(send.queue.back(), databaseName);
  lock.unlock(); // lock not needed past this point

  if (wasEmpty) {
    // The queue was empty before -> notify the network thread of a new available message
    NotifyNewSendMesssages();
  }
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
    ConnectToServer();

    if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(*socket), *ioCompletionPort, 0, 0)) {
      throw network::exception("Failed to associate the socket with the io completion port: ");
    }


    //TODO: Also start receiving server responses
    
    //TODO: listen to some event, which will terminate the client and close the connection properly
    while (true) {

      DWORD bytesTransferred;
      void* completionKey;
      OVERLAPPED* overlapped;
      GetQueuedCompletionStatus(*ioCompletionPort, &bytesTransferred, reinterpret_cast<ULONG_PTR*>(&completionKey), &overlapped, INFINITE);

      if (completionKey == &send) {
        if (!overlapped) {
          // Notification that we have new queued messages to send and are currently not sending anything
          SendMessagesFromQueue();
        } else {
          // Last send operation completed
          SendCompleted(bytesTransferred);
        }
      }
      
    }

    if (shutdown(*socket, SD_SEND) == SOCKET_ERROR) {
      throw network::exception("shutdown() failed with: ");
    }

  } catch (std::exception& ex) {
    //TODO: we should push the error just like any other message to the calling thread
    std::cerr << "FATAL: " << ex.what();
  }

}

void network::Client::SendMessagesFromQueue() {
  std::unique_lock<std::mutex> lock(send.mutex);
  if (send.queue.empty()) {
    return; // nothing to send
  }
  send.buffers.reserve(send.queue.size());
  for (auto& buffer : send.queue) {
    send.buffers.push_back({ static_cast<ULONG>(buffer.size()), buffer.data() });
  }
  lock.unlock(); // send.queue mutex not needed after this point

  // Reinitialize our overlapped
  send.overlapped = { 0 };
  if (WSASend(*socket, send.buffers.data(), send.buffers.size(), NULL, NULL, &send.overlapped, nullptr) == SOCKET_ERROR) {
    auto error = WSAGetLastError();
    if (error == WSA_IO_PENDING) {
      // send has been scheduled
      return;
    }

    // Send failed
    throw network::exception("WSASend() failed with: ", error);
  }

  // Otherwise send completed synchronously -> the message will be processed when handling the IOCOmpletionPort notification
}


void network::Client::SendCompleted(DWORD bytesTransferred) {
  if (!bytesTransferred) {
    // TODO: can this happen? If yes, is it an indication of a closed connection?
    throw network::exception("WSASend() transmitted 0 bytes!");
  }

  std::unique_lock<std::mutex> lock(send.mutex);

  // Remove fully transmitted buffers from the send buffer queue
  while (!send.buffers.empty() && bytesTransferred >= send.buffers.front().len) {
    bytesTransferred -= send.buffers.front().len;
    send.buffers.erase(send.buffers.begin());
    send.queue.pop_front();
  }

  // Update first partially transmitted buffer
  if (!send.buffers.empty() && bytesTransferred > 0) {
    assert(bytesTransferred < send.buffers.front().len); // if this fails, then WSASend() has sent more bytes than we requested, which should not be possible
    send.buffers.front().buf += bytesTransferred;
    send.buffers.front().len -= bytesTransferred;
    bytesTransferred = 0;
  }

  // Schedule the send of the next entries in the queue if there are more than currently scheduled
  if (send.queue.size() > send.buffers.size()) {
    for (int i = send.buffers.size(); i < send.queue.size(); ++i) {
      auto& entry = send.queue[i];
      send.buffers.push_back({ static_cast<ULONG>(entry.size()), entry.data() });
    }
  }
  lock.unlock(); // send.queue mutex not needed after this point

  // If there is more data to send, trigger another WSASend()
  if (!send.buffers.empty()) {
    send.overlapped = { 0 };
    if (WSASend(*socket, send.buffers.data(), send.buffers.size(), NULL, NULL, &send.overlapped, nullptr) == SOCKET_ERROR) {
      auto error = WSAGetLastError();
      if (error == WSA_IO_PENDING) {
        // send has been scheduled
        return;
      }

      // Send failed
      throw network::exception("WSASend() failed with: ", error);
    }
  }
  // otherwise send has completed synchronously, which we will handle in GetQueuedCompletionStatus()
}


void network::Client::NotifyNewSendMesssages() const {
  // Simply post a completion packet with SEND as completion handle and a nullptr overlapped
  PostQueuedCompletionStatus(*ioCompletionPort, 0, reinterpret_cast<ULONG_PTR>(&send), nullptr);
}


}
