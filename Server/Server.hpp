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

  network::Server server;
};

}}
