#include <network/IOCompletionPort.hpp>
#include <network/Network.hpp>

#include <cassert>

namespace blobs {
namespace network {


void IOCompletionPort::Create(DWORD maxNumberOfConcurrentThreads) {
  if (auto completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, maxNumberOfConcurrentThreads)) {
    // We assign after the check, because this function returns NULL instead of INVALID_HANDLE_VALUE upon failure and I don't want to extend
    // Resource<HANDLE> to check for NULL too
    *this = completionPort;
  } else {
    throw network::exception("CreateIoCompletionPort() failed with: ", GetLastError());
  }
}

void IOCompletionPort::AssociateSocket(SOCKET socket, IOCompletionHandler* completionHandler) {
  assert(HasHandle()); // Cannot associate a socket before initializing the completion port

  if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), **this, reinterpret_cast<ULONG_PTR>(completionHandler), 0)) {
    throw network::exception("Failed to associate socket with IO completioin port: ", GetLastError());
  }
}


void IOCompletionPort::PostIOCompletionPacket(IOCompletionHandler* completionHandler, DWORD bytesTransferred, OVERLAPPED* overlapped) {
  assert(HasHandle()); // Cannot associate a socket before initializing the completion port
  PostQueuedCompletionStatus(**this, bytesTransferred, reinterpret_cast<ULONG_PTR>(completionHandler), overlapped);
}




void IOCompletionPort::StopProcessing() {
  struct StoppingHandler : IOCompletionHandler {
    virtual void HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) {
      throw IOCompletionPort::Stopped();
    }
  };

  // Create an instance, which will not be deleted and post it to the completion port
  static StoppingHandler stopHandler;
  PostIOCompletionPacket(&stopHandler, 0, nullptr);
}



void IOCompletionPort::ProcessIOCompletionPacket() {
  DWORD bytesTransferred;
  IOCompletionHandler* completionHandler;
  OVERLAPPED* overlapped;
  GetQueuedCompletionStatus(**this, &bytesTransferred, reinterpret_cast<ULONG_PTR*>(&completionHandler), &overlapped, INFINITE);
  completionHandler->HandleIOCompletion(bytesTransferred, overlapped);
}


}}
