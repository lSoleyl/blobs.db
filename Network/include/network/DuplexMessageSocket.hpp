#pragma once

#include "IOCompletionHandler.hpp"
#include "Resource.hpp"
#include "message/Message.hpp"

#include <memory>

namespace blobs {
namespace network {

/** This abstract class implements a generic bidirectional message sender/receiver for blobs::network::Messages
 */
class DuplexMessageSocket : public IOCompletionHandler {
public:
  /** Handles the io completion callback for received/sent data
   */
  virtual void HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) override final;

protected:
  /** Takes ownership of the passed socket and associates it with the specified IO completion port
   *  and starts receiving messages immediately.
   */
  DuplexMessageSocket(Resource<SOCKET>&& socket, HANDLE ioCompletionPort);


  /** Schedules a WSARecv() call with an optional read buffer offset, which is used in case
   *  the read buffer contains too little data for even the message header to be processed and
   *  we have to wait for more data to arrive.
   * 
   * @param bufferOffset which byte offset in the recevie buffer should be the first to be written to
   */
  void ReceiveData(size_t bufferOffset = 0);

  /** Called by HandleIOCompletion() when new received data has arrived on this socket
   */
  void ProcessReceivedData(DWORD bytesTransferred);

  /** To be implemented by the inheriting class. Handle closed connection
   */
  virtual void HandleSocketClosed() = 0;

  /** To be implemented by the inheriting class. Handle a new message being fully received
   */
  virtual void HandleMessageReceived(std::unique_ptr<message::Message> message) = 0;

  struct Receive {
    WSAOVERLAPPED overlapped;
    WSABUF bufferInfo;
    char buffer[1024];
    DWORD flags;

    size_t writeOffset; // how many bytes of `message` have already been received
    std::unique_ptr<message::Message> message; // the current (incompletely) received message
  };

  Receive receive;
  Resource<SOCKET> socket;
};


}}