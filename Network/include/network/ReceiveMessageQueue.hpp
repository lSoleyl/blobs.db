#pragma once


#include "MessagePointer.hpp"

#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>


namespace blobs {
namespace network {


/** This datastructure is used in the client and server to safely transfer received messages from the network thread into
 *  the main thread.
 */
class ReceiveMessageQueue {
public:
  /** Called by the main thread. This waits until a new message is received and returns it
   */
  MessagePointer AwaitMessage();


  /** Called by the network thread to put a newly received message into the message queue
   */
  void MessageReceived(MessagePointer&& message);


private:
  std::mutex mutex;
  std::condition_variable messageReceived;
  std::deque<MessagePointer> queue;
};



}}