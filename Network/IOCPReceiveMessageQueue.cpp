#include <network/IOCPReceiveMessageQueue.hpp>

namespace blobs {
namespace network {


IOCPReceiveMessageQueue::IOCPReceiveMessageQueue(IOCompletionPort& ioCompletionPort, IOCompletionHandler& completionHandler) :
  ioCompletionPort(ioCompletionPort), completionHandler(completionHandler) {}


MessagePointer IOCPReceiveMessageQueue::FetchMessage() {
  MessagePointer message;
  std::unique_lock<std::mutex> lock(mutex);

  if (!queue.empty()) {
    message = std::move(queue.front());
    queue.pop_front();
  }

  return message;
}


void IOCPReceiveMessageQueue::MessageReceived(MessagePointer&& message) {
  std::unique_lock<std::mutex> lock(mutex);
  bool wasEmpty = queue.empty();
  queue.push_back(std::move(message));
  lock.unlock();
  if (wasEmpty) {
    // Notify IOCompletionPort about new message
    ioCompletionPort.PostIOCompletionPacket(&completionHandler);
  }
}


}}