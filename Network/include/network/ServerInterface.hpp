#pragma once

#include "MessagePointer.hpp"

namespace blobs {
namespace network {

/** Interface to be implemented by socket server and standalone server classes
 */
class ServerInterface {
public:
  /** The destructor should close all client connections and stop any running network thread and clean up all ressources.
   */
  virtual ~ServerInterface() {};

  /** Wait for the next sever message without a timeout
   */
  virtual MessagePointer AwaitMessage() = 0;

  /** Send an already allocated message to the specified client
   */
  virtual void SendMessageToClient(client_id client, MessagePointer message) = 0;
};

}}
