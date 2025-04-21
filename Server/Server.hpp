#pragma once


#include <network/Server.hpp>
#include <network/message/All.hpp>

namespace blobs {
namespace server {



class Server {
public:
  Server(int port);

  /** Main server network message processing loop.
   */
  void ServerMain();


private:

  void HandleConnectionOpened(network::MessagePointer_T<network::message::ConnectionOpened> message);
  void HandleConnectionClosed(network::MessagePointer_T<network::message::ConnectionClosed> message);

  void HandleDatabaseOpen(network::MessagePointer_T<network::message::DatabaseOpen> message);
  void HandleDatabaseClose(network::MessagePointer_T<network::message::DatabaseClose> message);

  void HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message);

  void HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message);

  /** Primitive logging of incoming messages.
   */
  void LogMessage(const network::message::Message& message);

  /** Releases the client's locks from all opened databases and removes all queued read requests and
   *  checks whether any client can now satisfy his outstanding read requests.
   */
  void AbortTransaction(client_id clientId);

  network::Server server;
};

}}
