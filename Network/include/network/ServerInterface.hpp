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

  /** Fetch the next message from the server's receive queue. A null message is returned if no message is queued.
   */
  virtual MessagePointer FetchMessage() = 0;

  /** Send an already allocated message to the specified client
   */
  virtual void SendMessageToClient(client_id client, MessagePointer message) = 0;
};

}}
