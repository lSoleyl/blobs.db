#pragma once

#include "IOCompletionPort.hpp"
#include "message/Message.hpp"

#include <memory>
#include <mutex>
#include <deque>
#include <vector>

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
  /** Default constructor performs no initialization, InitializeMessageSocket() must be called afterwards
   */
  DuplexMessageSocket();

  /** Default initializes and immediately calls InitializeMessageSocket() to take ownership of the socket, 
   *  associate it with the given IO completion port and immediately start receiving messages.
   */
  DuplexMessageSocket(Resource<SOCKET>&& socket, IOCompletionPort& ioCompletionPort);

  /** Takes ownership of the passed socket and associates it with the specified IO completion port
   *  and starts receiving messages immediately.
   */
  void InitializeMessageSocket(Resource<SOCKET>&& socket, IOCompletionPort& ioCompletionPort);

  /** Simply calls shutdown on the socket (if initialized)
   */
  void CloseSocket();

  /** To be implemented by the inheriting class. Handle closed connection
   */
  virtual void HandleSocketClosed() = 0;

  /** To be implemented by the inheriting class. Handle a new message being fully received
   */
  virtual void HandleMessageReceived(std::unique_ptr<message::Message> message) = 0;

private:
  /** This structure is used to give safe access send queue while the token exists on the stack.
   *  Upon destruction the token will notify the duplex socket in case the queue was empty and now isn't anymore.
   */
  struct SendQueueAccessToken {
    SendQueueAccessToken(DuplexMessageSocket& socket);
    ~SendQueueAccessToken();

    /** Places a new buffer into the send queue and returns a reference to it.
     */
    std::vector<char>& operator*();
  private:
    DuplexMessageSocket& socket;
    std::unique_lock<std::mutex> lock; // send queue lock
    bool wasEmpty, bufferCreated;
  };

protected:
  /** Use this method to get access to the send queue. This will properly synchronize the write access and
   *  will return a token, which can be dereferenced to get a send buffer to fill.
   */
  SendQueueAccessToken AccessSendQueue();

private:
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
  
  /** Checks whether there are messages to transmit and if so, will schedule a WSASend() for these messages
   */
  void SendData();

  /** Called by HandleIOCompletion() when sending data has completed
   */
  void ProcessSentData(DWORD bytesTransferred);


  struct Receive {
    WSAOVERLAPPED overlapped;
    WSABUF bufferInfo;
    char buffer[1024];
    DWORD flags;

    size_t writeOffset; // how many bytes of `message` have already been received
    std::unique_ptr<message::Message> message; // the current (incompletely) received message
  };

  struct Send {
    std::vector<WSABUF> buffers; // Send buffers of the current send operation. If this is not empty, then a WSASend() is currently in progress
    std::deque<std::vector<char>> queue; // TODO: what is the correct type here?
    WSAOVERLAPPED overlapped;
    std::mutex mutex; // synchronizing access to the sendQueue between the network thread and the main thread
  };

  Receive receive;
  Send send;
  Resource<SOCKET> socket;
  IOCompletionPort* ioCompletionPort; // the completion port, this socket is associated with
};


}}