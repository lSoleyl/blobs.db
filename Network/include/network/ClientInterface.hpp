#pragma once

#include "MessagePointer.hpp"


namespace blobs {
namespace network {

class ClientInterface {
public:
  /** Deleting the client instance should close the existing server connection and stop all running network threads.
   */
  virtual ~ClientInterface() {};

  /** Used to send already allocated messages to the server (SendMessage() is sadly already in use by WinAPI)
   */
  virtual void SendMessageToServer(MessagePointer&& message) = 0;

  /** Wait for the next sever message without a timeout
   */
  virtual MessagePointer AwaitMessage() = 0;
};


}
}