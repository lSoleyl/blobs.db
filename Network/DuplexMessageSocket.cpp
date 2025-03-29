#include <network/DuplexMessageSocket.hpp>
#include <network/Network.hpp>

#include <cassert>

namespace blobs {
namespace network {

DuplexMessageSocket::DuplexMessageSocket() : ioCompletionPort(INVALID_HANDLE_VALUE) {}

DuplexMessageSocket::DuplexMessageSocket(Resource<SOCKET>&& socket, HANDLE ioCompletionPort) {
  InitializeMessageSocket(std::move(socket), ioCompletionPort);
}

void DuplexMessageSocket::InitializeMessageSocket(Resource<SOCKET>&& socket, HANDLE ioCompletionPort) {
  this->socket = std::move(socket);
  this->ioCompletionPort = ioCompletionPort;

  // Associate the given socket with our io completion port
  if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(*this->socket), ioCompletionPort, reinterpret_cast<ULONG_PTR>(this), 0)) {
    throw network::exception("Failed to associate socket with IOCompletionPort: ", GetLastError());
  }

  // Immediately start listening
  ReceiveData();

  // Send any data, which may already be in the send queue
  SendData();
}

void DuplexMessageSocket::CloseSocket() {
  if (socket) {
    if (shutdown(*socket, SD_BOTH) == SOCKET_ERROR) {
      throw network::exception("shutdown() failed with: ");
    }
    socket.Reset();
  }
}


void DuplexMessageSocket::HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) {
  if (bytesTransferred == 0) {
    // no bytes being transferred on a stream socket is used to indicate a closed connection
    HandleSocketClosed();
    //TODO: should we also cancel outstanding IO on this socket here?
  } else if (overlapped == &receive.overlapped) {
    // We received some data
    ProcessReceivedData(bytesTransferred);
  } else if (overlapped == &send.overlapped) {
    // We completed sending data
    ProcessSentData(bytesTransferred);
  }
}




void DuplexMessageSocket::ReceiveData(size_t bufferOffset) {
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


void DuplexMessageSocket::ProcessReceivedData(DWORD bytesTransferred) {
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
        HandleMessageReceived(std::move(receive.message));
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

void DuplexMessageSocket::SendData() {
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


void DuplexMessageSocket::ProcessSentData(DWORD bytesTransferred) {
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
}

DuplexMessageSocket::SendQueueAccessToken DuplexMessageSocket::AccessSendQueue() {
  return SendQueueAccessToken(*this);
}

DuplexMessageSocket::SendQueueAccessToken::SendQueueAccessToken(DuplexMessageSocket& socket) : socket(socket), lock(socket.send.mutex), bufferCreated(false) {
  wasEmpty = socket.send.queue.empty();
}

DuplexMessageSocket::SendQueueAccessToken::~SendQueueAccessToken() {
  lock.unlock(); // not needed anymore
  if (wasEmpty && bufferCreated) {
    // Notify the socket about new available messages to send
    PostQueuedCompletionStatus(socket.ioCompletionPort, 1, reinterpret_cast<ULONG_PTR>(this), nullptr);
  }
}

std::vector<char>& DuplexMessageSocket::SendQueueAccessToken::operator*() {
  socket.send.queue.push_back({});
  bufferCreated = true;
  return socket.send.queue.back();
}



}}