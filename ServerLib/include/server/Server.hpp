#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <network/ServerInterface.hpp>
#include <network/message/All.hpp>
#include <network/IOCompletionPort.hpp>
#include <network/IOCPReceiveMessageQueue.hpp>

namespace blobs {
class Configuration;
namespace server {

class Database;
class Client;
class Scheduler;

class Server {
public:
  Server();
  Server(const blobs::Configuration& config);
  ~Server();

  /** Main server network message processing loop.
   *  This method should only ever be called once.
   */
  void ServerMain();

  /** This method will trigger the shutdown of the server, which will lead to the server exiting the ServerMain() loop
   *  This method does not wait for the ServerMain() to be exited.
   */
  void BeginShutdown();

  /** Static access to the only running server instance
   */
  static Server& Instance();

  /** Access to server's completion port to be able to post completion handlers from other threads to be processed in the server's main thread.
   */
  network::IOCompletionPort& GetCompletionPort();

  /** Access to the server's task scheduler
   */
  Scheduler& GetScheduler();

  /** Returns the configured database close delay for this server.
   */
  std::chrono::milliseconds GetDatabaseCloseDelay() const;

  /** Resolves the passed database name into an absolute path base don the configured dbRootDir.
   *  Returns an empty optional if databaseName refers to a path located outside of dbRootDir.
   */
  std::optional<std::wstring> GetResolvedDatabasePath(std::string_view databaseName) const;

  /** Called by Database to notify the server that a queued blobs read message has timed out and the client should be informed about that.
   */
  void ReadTimedOut(const network::message::BlobsRead& message);

private:
  /** Processes any outstanding not yet processed network messages
   */
  void ProcessReceivedMessages();


  /** Shorthand for server->SendMessageToClient(...)
   */
  void SendMessageToClient(client_id clientId, network::MessagePointer message);

  void HandleConnectionOpened(network::MessagePointer_T<network::message::ConnectionOpened> message);
  void HandleConnectionClosed(network::MessagePointer_T<network::message::ConnectionClosed> message);

  void HandleDatabaseOpen(network::MessagePointer_T<network::message::DatabaseOpen> message);
  void HandleDatabaseClose(network::MessagePointer_T<network::message::DatabaseClose> message);

  void HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message);

  void HandleTransactionBegin(network::MessagePointer_T<network::message::TransactionBegin> message);
  void HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message);
  void HandleTransactionCommit(network::MessagePointer_T<network::message::TransactionCommit> message);

  
  using ImplicitLocks = std::vector<std::pair<database_id, std::vector<BlobLocation>>>;

  /** Validates all commit message stored for that client and returns a result being either SUCCESS if the commit is
   *  valid or holding the reason for why this commit failed.
   * 
   * @param client the client whose accumulated commit messages should be validated
   * @param implicitWriteLocks this data structure will collect write locks which the client implcitly hold by creating the said blobs
   */
  network::message::TransactionCommitResponse::Result ValidateCommitMessages(const blobs::server::Client& client, ImplicitLocks& implicitWriteLocks) const;

  /** Abort a currently running transaction commit and the corresponding transaction. 
   *  Either because the sent commit messages were invalid or because the client sent a wrong message during commit.
   */ 
  void AbortTransactionCommit(blobs::server::Client& client);
  


  /** This method will for each requested blob in the blobs read message ensure that it actually exists in the database 
   *  and if any of the requested blobs does not exist, the method will respond with a matching error message and return false.
   * 
   * @pre the caller should already have validated that:
   *         - the requested locks match the mvcc mode of the database
   *         - the client is inside a transaction
   * 
   * @param database the database this message reads from
   * @param message the message to check all requested blobs for existence
   * @param isMVCC set to true if the read is performed on the client's current MVCC snapshot
   * 
   * @return true if all blobs exist, false if at least one does not. In that case an error response has already been sent to the client.
   */
  bool EnsureRequestedBlobsExist(blobs::server::Database& database, const network::message::BlobsRead& message, bool isMVCC);


  /** This method will attempt to acquire the locks for the requested pages and directly reply to the client upon success.
   *
   * @return true if the message has been fully handled (includes sending an error to the client)
   *         false if the message couldn't be handled due to conflicting locks.
   */
  bool TryHandleBlobsRead(const network::message::BlobsRead& message);


  /** This method is called by TryHandleBlobsRead after ensuring that all requested blobs exist and 
   *  all locks have been acquired to actually construct the blobs read response for the requested blobs
   *  and send the response(s) to the client.
   * 
   * @param database the database from which the blobs are read
   * @param message the message requesting the blobs
   * @param isMVCC whether to read from the MVCC snapshot or the active database
   */ 
  void BuildAndSendBlobsReadResponse(blobs::server::Database& database, const network::message::BlobsRead& message, bool isMVCC);


  struct BlobData {
    std::vector<char> content;
    blobs::commit_id commitId;
  };


  /** This method is called by TryHandleBlobsRead when receiving a blobs read request for the blob id list.
   *  The caller must ensure that the referenced cluster exists and client already holds the required locks.
   *
   * @param database the database to read the blob list from
   * @param segmentId the segment to read the blob list from
   * @param clusterId the cluster to read the blob list from
   * @param cacheCommitId the commit id of the client's cached version of the blob list blob (as specified in the request)
   * @param isMVCC whether the client should read the blob list from the MVCC snapshot or the update snapshot
   *
   * @return the content of the cluster list blob or nullopt if the client's version of the blob is already up to date
   */
  std::optional<BlobData> HandleBlobListId(blobs::server::Database& database, blobs::segment_id segmentId, blobs::cluster_id clusterId, blobs::commit_id cacheCommitId, bool isMVCC);

  /** This method is called by TryHandleBlobsRead when receiving a blobs read request for the cluster id list.
   *  The caller must ensure that the referenced segment exists and client already holds the required locks.
   * 
   * @param database the database to read the cluster list from
   * @param segmentId the segment to read the cluster list from
   * @param cacheCommitId the commit id of the client's cached version of the cluster list blob (as specified in the request)
   * @param isMVCC whether the client should read the cluster list from the MVCC snapshot or the update snapshot
   *
   * @return the content of the cluster list blob or nullopt if the client's version of the blob is already up to date
   */
  std::optional<BlobData> HandleClusterListId(blobs::server::Database& database, blobs::segment_id segmentId, blobs::commit_id cacheCommitId, bool isMVCC);


  /** This method is called by TryHandleBlobsRead when receiving a blobs read request for the segment id list.
   *  The caller must ensure that the client already holds the required locks.
   * 
   * @param database the database to read the segment list from
   * @param cacheCommitId the commit id of the client's cached version of the segment list blob (as specified in the request)
   * @param isMVCC whether the client should read the segment list from the MVCC snapshot or the update snapshot
   * 
   * @return the content of the segment list blob or nullopt if the client's version of the blob is already up to date
   */
  std::optional<BlobData> HandleSegmentListId(blobs::server::Database& database, blobs::commit_id cacheCommitId, bool isMVCC);


  /** This method is called by TryHandleBlobsRead when receiving a blobs read request for a regular blob.
   *  The caller must ensure that the blob exists and the client already holds the required locks
   * 
   * @param database the database to read the blob from
   * @param requestedBlob the blob location and cacheCommitId of the requested blob
   * @param isMVCC whether the client should read the blob from the MVCC snapshot or the update snapshot
   * 
   * @return the content of the blob or nullopt if the client's version of the blob is already up to date
   */
  std::optional<BlobData> HandleRegularBlobRead(blobs::server::Database& database, const blobs::network::message::BlobsRead::BlobAddress& requestedBlob, bool isMVCC);


  /** Primitive logging of incoming messages.
   */
  void LogMessage(const network::message::Message& message);

  /** Releases the client's locks from all opened databases and removes all queued read requests and
   *  checks whether any client can now satisfy his outstanding read requests.
   */
  void AbortTransaction(blobs::server::Client& client, bool releaseAllLocks);

  /** Attempts to process as many queued read operations as possible for the specified database.
   */
  void TryProcessQueuedReads(blobs::server::Database& database);

  /** Should be called whenever a client commits or aborts his transaction and thus releases all locks across all its databases
   *  This will then trigger processing of queued messages in these databases.
   */
  void ClientTransactionEnded(const blobs::server::Client& client);

  std::unique_ptr<network::ServerInterface> server;

  /** The IO completion port used to trigger processing in the server's main event loop.
   *  This includes events like the message receive queue not being empty anymore and events like
   *  the completion of a database load.
   */
  network::IOCompletionPort ioCompletionPort;

  /** Completion handler to be called when receiving new messages in the message queue and the message queue is not empty anymore
   */
  class MessageReceiveHandler : public network::IOCompletionHandler {
  public:
    MessageReceiveHandler(Server& server);
    virtual void HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) override;
  private:
    Server& server;

  } messageReceived;

  /** The server receives all messages from clients on this queue (through the `server` interface)
   */
  network::IOCPReceiveMessageQueue receiveQueue;


  /** The single server instance (we currently do not support multiple server instances in a single process due to Database for example holding a static map of opened databases)
   */
  static Server* instance;

  /** The location where databases are stored by the server. All database paths will be relative to that specified root
   *  default: ".\databases"
   */
  std::optional<std::wstring> dbRootDir;

  /** Optional close delay for databases. This may help avoid performance issues when frequently reopening a database (for whatever reason) by keeping
   *  it alive in the server's memory for a while longer. This may also be useful to avoid data loss between clients for in-memory databases (because
   *  an in-memory database is deleted once it is closed). Though a too high value may lead to keeping more data in server memory than necessary.
   * 
   * default: 0 = no delay
   */
  const std::chrono::milliseconds databaseCloseDelay;

  /** The scheduler is created inside the ServerMain() method and is used to schedule delayed invocations
   *  of certain tasks for time based events.
   */
  std::unique_ptr<Scheduler> scheduler;
};

}
}