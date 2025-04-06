#include <network/ReceiveMessageQueue.hpp>

namespace blobs {
namespace network {


MessagePointer ReceiveMessageQueue::AwaitMessage() {
  MessagePointer result;
  std::unique_lock<std::mutex> lock(mutex);

  // Wait for message to arrive if the queue is empty
  while (queue.empty()) {
    messageReceived.wait(lock);
  }

  result = std::move(queue.front());
  queue.pop_front();
  return result;
}


void ReceiveMessageQueue::MessageReceived(MessagePointer&& message) {
  std::unique_lock<std::mutex> lock(mutex);
  bool wasEmpty = queue.empty();
  queue.push_back(std::move(message));
  lock.unlock();
  if (wasEmpty) {
    // Wake up waiting thread (if any)
    messageReceived.notify_one();
  }
}


}}