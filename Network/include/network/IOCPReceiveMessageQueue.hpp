#pragma once

#include "MessagePointer.hpp"
#include "IOCompletionPort.hpp"

#include <mutex>
#include <deque>

namespace blobs {
namespace network {


/** This class works similar to the ReceiveMessageQueue, but instead of explicitly awaiting new messages and blocking the current thread
 *  on a condition_variable, we here notify the processing thread on the provided IOCompletionPort by posting the specified IOCompletionHandler to it.
 *  
 *  This class also only provides non blocking read operations and is used by the server main thread to be able to process multiple different events on the 
 *  same thread.
 */
class IOCPReceiveMessageQueue {
public:
  /** Instantiates a new receive message queue posting its notifications to the specified completion port with 
   *  the provided completion handler
   * 
   * @param ioCompletionPort the completion port to post the notifications to
   * @param completionHandler the completion handler to post as notification for new messages /must live at least as long this receive queue)
   */
  IOCPReceiveMessageQueue(IOCompletionPort& ioCompletionPort, IOCompletionHandler& completionHandler);

  /** Dequeues the next message and returns it (if there is a message queued).
   *  return a null message if no message is queued.
   */
  MessagePointer FetchMessage();


  /** Called by the network thread to put a newly received message into the message queue (and potentially notify the completion port about the non empty queue)
   */
  void MessageReceived(MessagePointer&& message);


private:
  std::mutex mutex;
  std::deque<MessagePointer> queue;

  IOCompletionPort& ioCompletionPort;
  IOCompletionHandler& completionHandler;
};


}}