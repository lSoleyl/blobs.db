#pragma once

#include "Resource.hpp"
#include "IOCompletionHandler.hpp"
#include "..\win_include.hpp"

#include <functional>

namespace blobs {
namespace network {


/** An IO completion port resource handle with additional methods providing more type safety by 
 *  requiring completion keys to always be IOCompletionHandlers
 */
class IOCompletionPort : public Resource<HANDLE> {
public:
  /** Initializes this resource handle with a newly created IO completion port with the specified concurrency
   *
   * @param maxNumberOfConcurrentThreads maximum number of threads, which the port will allow to run at the same time (0 = unlimited).
   */
  void Create(DWORD maxNumberOfConcurrentThreads = 0);

  /** Associates the given network socket with this completion port and assigns a completion handler to be run when 
   *  completing async operations on that socket.
   */
  void AssociateSocket(SOCKET socket, IOCompletionHandler* completionHandler);

  /** A wrapper around PostQueuedCompletionStatus() to post a completion packet to this IO completion port for the specified completion handler
   */
  void PostIOCompletionPacket(IOCompletionHandler* completionHandler, DWORD bytesTransferred, OVERLAPPED* overlapped);

  /** This helper method will wrap the function object into an IOCompletionHandler and post it to have it being processed by
   *  ProcessIOCompletionPacket()
   */
  void PostSimpleTask(std::function<void()>&& task);


  /** This method may be called from within another thread to interrupt a currently waiting ProcessIOCompletionPort() and exit the
   *  surrounding loop by throwing an exception.
   */
  void StopProcessing();


  // Used as an exception to be thrown by ProceessIOCompletionPacket() if processing has been stopped
  struct Stopped {};

  /** Calls GetQueuedCompletionStatus() to wait for a new completion packet on this IO completion port and processes it by calling
   *  the corresponding completion handler
   * 
   * @throws false if the processing loop should be quit
   */
  void ProcessIOCompletionPacket();


  /** Allow assignment from Resource<HANDLE>
   */
  using Resource::operator=;






};











}}