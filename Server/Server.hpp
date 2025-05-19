#pragma once


#include <network/Server.hpp>
#include <network/message/All.hpp>

namespace blobs {
namespace server {

class Database;
class Client;

class Server {
public:
  Server(int port = 8108);

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

  /** This method will attempt to acquire the locks for the requested pages and directly reply to the client upon success.
   *
   * @return true if the message has been fully handled (includes sending an error to the client)
   *         false if the message couldn't be handled due to conflicting locks.
   */
  bool TryHandleBlobsRead(const network::message::BlobsRead& message);

  


  /** Primitive logging of incoming messages.
   */
  void LogMessage(const network::message::Message& message);

  /** Releases the client's locks from all opened databases and removes all queued read requests and
   *  checks whether any client can now satisfy his outstanding read requests.
   */
  void AbortTransaction(client_id clientId);

  /** Attempts to process as many queued read operations as possible for the specified database.
   */
  void TryProcessQueuedReads(blobs::server::Database& database);

  /** Should be called whenever a client commits or aborts his transaction and thus releases all locks across all its databases
   *  This will then trigger processing of queued messages in these databases.
   */
  void ClientTransactionEnded(const blobs::server::Client& client);

  network::Server server;
};

}}
