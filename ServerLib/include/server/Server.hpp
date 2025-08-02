#pragma once


#include <network/ServerInterface.hpp>
#include <network/message/All.hpp>

namespace blobs {
namespace server {

class Database;
class Client;

class Server {
public:
  Server(int port = 8108);
  ~Server();

  /** Main server network message processing loop.
   */
  void ServerMain();

  /** This method will trigger the shutdown of the server, which will lead to the server exiting the ServerMain() loop
   *  This method does not wait for the ServerMain() to be exited.
   */
  void BeginShutdown();

  /** Static access to the only running server instance
   */
  static Server& Instance();


  /** When a database completes/fails loading this will be called for each client, which registered itself to the database
   */
  void HandleDatabaseOpenResult(Database& database, network::message::DatabaseOpenResponse::Result result, client_id clientId);



private:
  /** Shorthand for server->SendMessageToClient(...)
   */
  void SendMessageToClient(client_id clientId, network::MessagePointer message);

  void HandleConnectionOpened(network::MessagePointer_T<network::message::ConnectionOpened> message);
  void HandleConnectionClosed(network::MessagePointer_T<network::message::ConnectionClosed> message);

  void HandleDatabaseOpen(network::MessagePointer_T<network::message::DatabaseOpen> message);
  void HandleDatabaseClose(network::MessagePointer_T<network::message::DatabaseClose> message);

  void HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message);

  void HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message);
  void HandleTransactionCommit(network::MessagePointer_T<network::message::TransactionCommit> message);

  /** Validates all commit message stored for that client and returns a result being either SUCCESS if the commit is
   *  valid or holding the reason for why this commit failed.
   */
  network::message::TransactionCommitResponse::Result ValidateCommitMessages(const blobs::server::Client& client) const;

  /** Abort a currently running transaction commit and the corresponding transaction. 
   *  Either because the sent commit messages were invalid or because the client sent a wrong message during commit.
   */
  void AbortTransactionCommit(blobs::server::Client& client);
  

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
  void AbortTransaction(blobs::server::Client& clientId);

  /** Attempts to process as many queued read operations as possible for the specified database.
   */
  void TryProcessQueuedReads(blobs::server::Database& database);

  /** Should be called whenever a client commits or aborts his transaction and thus releases all locks across all its databases
   *  This will then trigger processing of queued messages in these databases.
   */
  void ClientTransactionEnded(const blobs::server::Client& client);

  std::unique_ptr<network::ServerInterface> server;

  /** The single server instance (we currently do not support multiple server instances in a single process due to Database for example holding a static map of opened databases)
   */
  static Server* instance;
};

}}
