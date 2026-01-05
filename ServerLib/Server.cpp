#include "pch.hpp"
#include "include/server/Server.hpp"
#include "include/server/Client.hpp"
#include "include/server/Logging.hpp"

#include <network/Factory.hpp>
#include <common/Encoding.hpp>

#include <iostream>
#include <sstream>

namespace blobs {
namespace server {

Server* Server::instance = nullptr;

Server::Server(int port) : messageReceived(*this), receiveQueue(ioCompletionPort, messageReceived) {
  assert(!instance); // Only one server instance is allowed to run at a time
  instance = this;

  ioCompletionPort.Create();
  server = network::Factory::Instance().CreateServer(receiveQueue, port);
}

Server::~Server() {
  instance = nullptr;
}

void Server::ServerMain() {
  try {
    while (true) {
      ioCompletionPort.ProcessIOCompletionPacket();
    }
  } catch (network::IOCompletionPort::Stopped&) {
    BLOBS_LOG_DEBUG("Shutdown signal received, exiting Server::ServerMain()");
    return;
  }
}

void Server::BeginShutdown() {
  // This will post a stop handler to the IOCompletionPort, which will lead to the server exiting the main server loop
  ioCompletionPort.StopProcessing();
}

Server& Server::Instance() {
  assert(instance);
  return *instance;
}

network::IOCompletionPort& Server::GetCompletionPort() {
  return ioCompletionPort;
}

void Server::ProcessReceivedMessages() {

  // Process all outstanding messages
  while (auto message = server->FetchMessage()) {
    LogMessage(*message);

    TODO("Determine the client of the message here already and unless it is ConnectionOpened, pass it to the Handle-Method");
    TODO("Unless the message is ConnectionOpened, we can then check whether the client accepts the message.");
    TODO("Alternatively we would have to implement that check in each Handle()-Method, which wouldn't be nice.");

    switch (message->type) {
      case network::message::Type::ConnectionOpened:
        HandleConnectionOpened(message.Cast<network::message::ConnectionOpened>());
        break;

      case network::message::Type::ConnectionClosed:
        HandleConnectionClosed(message.Cast<network::message::ConnectionClosed>());
        break;

      case network::message::Type::DatabaseOpen:
        HandleDatabaseOpen(message.Cast<network::message::DatabaseOpen>());
        break;

      case network::message::Type::DatabaseClose:
        HandleDatabaseClose(message.Cast<network::message::DatabaseClose>());
        break;

      case network::message::Type::BlobsRead:
        HandleBlobsRead(message.Cast<network::message::BlobsRead>());
        break;


      case network::message::Type::TransactionBegin:
        HandleTransactionBegin(message.Cast<network::message::TransactionBegin>());
        break;

      case network::message::Type::TransactionAbort:
        HandleTransactionAbort(message.Cast<network::message::TransactionAbort>());
        break;

      case network::message::Type::TransactionCommit:
        HandleTransactionCommit(message.Cast<network::message::TransactionCommit>());
        break;


        //TODO: handle other messages

      default:
        BLOBS_LOG_ERROR("Server received message of unexpected type " << message->type << "(" << static_cast<int>(message->type) << ") from client " << message->clientId);
        break;
    }
  }
}

void Server::HandleDatabaseOpenResult(Database& database, network::message::DatabaseOpenResponse::Result result, client_id clientId) {
  TODO("Handle client closing connection before the databse completed opening");
  auto& client = server::Client::Get(clientId);


  if (client.HasDatabaseOpened(database)) {
    // Client already opened this same database before without closing it. We don't allow this would lead to the client
    // holding two separate blob caches and we would have to add a refcount to properly handle closing the database.
    SendMessageToClient(client.id, network::message::DatabaseOpenResponse::Create(network::message::DatabaseOpenResponse::Result::DATABASE_ALREADY_OPEN, 0));
  } else {
    if (result != network::message::DatabaseOpenResponse::Result::SUCCESS) {
      // Database loading failed -> send error code back to client
      SendMessageToClient(client.id, network::message::DatabaseOpenResponse::Create(result, 0));
    } else {
      try {
        // Mark the databse as opened in the client (if possible) and reply with success status code
        SendMessageToClient(client.id, network::message::DatabaseOpenResponse::Create(network::message::DatabaseOpenResponse::Result::SUCCESS, client.OpenDatabase(database)));
      } catch (std::exception&) {
        SendMessageToClient(client.id, network::message::DatabaseOpenResponse::Create(network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN, 0));
      }
    }
  }
}





void Server::SendMessageToClient(client_id clientId, network::MessagePointer message) {
  server->SendMessageToClient(clientId, std::move(message));
}


void Server::HandleConnectionOpened(network::MessagePointer_T<network::message::ConnectionOpened> message) {
  // New client connected (initialize logical client data)
  BLOBS_LOG_INFO("Client[" << message->clientId << "] connected from " << message->GetRemoteIp());
  Client::ClientConnected(message->clientId);
}

void Server::HandleConnectionClosed(network::MessagePointer_T<network::message::ConnectionClosed> message) {
  // A client closed the connection
  BLOBS_LOG_INFO("Client[" << message->clientId << "] disconnected");
  auto& client = Client::Get(message->clientId);

  // Abort any running transaction, release any held locks
  // We call Server::AbortTransaction(), not Client::AbortTransaction() to also check whether any outstanding reads
  // can be satisfied now that the client's locks have been released.
  AbortTransaction(client, true/*release all locks*/);

  // Close all opened databases. This will also release any still held sticky locks
  client.CloseAllDatabases();
}



void Server::HandleDatabaseOpen(network::MessagePointer_T<network::message::DatabaseOpen> message) {
  // OpenDB request
  server::Database::Open(message->GetDatabaseName(), message->clientId);
}


void Server::HandleDatabaseClose(network::MessagePointer_T<network::message::DatabaseClose> message) {
  auto& client = server::Client::Get(message->clientId);
  if (client.IsInsideTransaction()) {
    // This is actually being prevented by the client lib, but just in case client transaction is out of sync -> check it here to be safe
    SendMessageToClient(client.id, network::message::DatabaseCloseResponse::Create(network::message::DatabaseCloseResponse::Result::TRANSACTION_IN_PROGRESS));
    return;
  }

  if (client.CloseDatabase(message->databaseId)) {
    // Successfully closed database
    SendMessageToClient(client.id, network::message::DatabaseCloseResponse::Create(network::message::DatabaseCloseResponse::Result::SUCCESS));
  } else {
    // Client didn't open this database or already closed it
    SendMessageToClient(client.id, network::message::DatabaseCloseResponse::Create(network::message::DatabaseCloseResponse::Result::DATABASE_NOT_OPEN));
  }

  
  TODO("If this was the last database, then we can also just close the connection");
}


void Server::HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message) {
  if (!TryHandleBlobsRead(*message)) {
    // Cannot immediately reply to message because of locking conflicts
    auto& client = server::Client::Get(message->clientId);
    auto database = client.GetDatabase(message->databaseId);
    assert(database); // <- TryHandleBlobsRead() would have returned true otherwise

    // Queue this message to the database to be processed as soon as the conflicting locks are released
    if (auto deadlock = database->QueueReadCheckDeadlock(std::move(message))) {
      // Deadlock detected -> one clients transaction must be terminated to continue.
      TODO("Don't always kill the last client, support something like a transaction priority");
      
      TODO("Support a client display name, which should be used here instead of just the client id");
      TODO("Fetch the hostname of the client and add it to the error description");

      std::ostringstream details;
      details << "Deadlock detected!\n"
        << "Client " << deadlock->requests[0].client << " attempts to " << (deadlock->requests[0].writeLock ? "write" : "read") << " lock " << deadlock->requests[0].location
        << " - conflicting lock held by Client " << deadlock->requests[1].client << "\n"
        << "Client " << deadlock->requests[1].client << " attempts to " << (deadlock->requests[1].writeLock ? "write" : "read") << " lock " << deadlock->requests[1].location
        << " - conflicting lock held by Client " << deadlock->requests[0].client << "\n"
      ;

      SendMessageToClient(client.id, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::DEADLOCK, details.str()));
      // Abort the client's current transaction (this client is the deadlock victim) - This will also remove any queued reads of that client from the queue
      // Aborting the transaction will also trigger processing of any outstanding reads, which may now be possible to complete
      AbortTransaction(client, false);
    }
  }
}


void Server::HandleTransactionBegin(network::MessagePointer_T<network::message::TransactionBegin> message) {
  auto& client = server::Client::Get(message->clientId);

  if (client.IsInsideTransaction()) {
    // Cannot start a transaction if the client is already inside one
    SendMessageToClient(client.id, network::message::TransactionBeginResponse::CreateError(network::message::TransactionBeginResponse::Result::ERROR_ALREADY_IN_TRANSACTION));
    return;
  }

  TODO("Support a MVCC transaction in the future by fixing a snapshot of the database");
  client.BeginTransaction();

  // FIXME STICKY Client may request to release additional locks... we should merge this info

  SendMessageToClient(client.id, client.ConstructTransactionBeginResponse());
}


void Server::HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message) {
  // FIXME STICKY the client should be able to specify in the TransactionAbort message whether to release all locks now or to keep them around
  AbortTransaction(server::Client::Get(message->clientId), false);
}


void Server::HandleTransactionCommit(network::MessagePointer_T<network::message::TransactionCommit> message) {
  using TransactionCommitPtr = network::MessagePointer_T<network::message::TransactionCommit>;

  // For now only handle simple case with a single commit message
  auto& client = server::Client::Get(message->clientId);


  if (!client.IsInsideTransaction()) {
    // Cannot commit if no transaction is in progress -> respond with error message and ignore the message
    server->SendMessageToClient(client.id, network::message::TransactionCommitResponse::CreateError(network::message::TransactionCommitResponse::Result::NO_TRANSACTION_IN_PROGRESS));
    return;
  }

  // To handle multi message commits we push the messages into a commit message vector, which will also
  // make the server reject any other messages for this client.
  client.commitMessages.push_back(std::move(message));

  if (!client.commitMessages.back()->hasFollowMessage) {
    // This is the final commit message in a list of commit messages -> validate the commit.
    // We must only validate the commit messages AFTER we received every commit message of a multipart commit, because the client
    // does not expect any response to partial commit messages, as this would only slow down the commit process when commiting to multiple
    // databases at once.


    // The client doesn't actually need to acquire a lock to own a lock. A client which creates a new blob implicitly holds a write lock onto it.
    // This write lock must be preserved as sticky lock across transactions, so we ask ValidateCommitMessage() to track all these implicitly acquired (write) locks.
    ImplicitLocks implicitWriteLocks;
    auto result = ValidateCommitMessages(client, implicitWriteLocks);
    if (result == network::message::TransactionCommitResponse::Result::SUCCESS) {
      // Commit messages are valid
      
      // Calculate changes to transient database state
      std::vector<std::pair<database_id, Database::CommitResult>> commitResults;
      for (auto pos = client.commitMessages.data(), end = pos + client.commitMessages.size(); pos != end;) {
        // Determine all messages, which apply to the same database
        auto dbId = (*pos)->databaseId;
        auto dbIdEnd = std::find_if(pos, end, [dbId](TransactionCommitPtr& message) { return message->databaseId != dbId; });

        // The range [pos;dbIdEnd) contains all commit messages, which apply to the same database, now apply them all in a batch.
        // ValidateCommitMessages() already ensures that the commit messages are grouped by database id
        auto database = client.GetDatabase(dbId);
        commitResults.emplace_back(dbId, database->CalculateCommitResult(pos, dbIdEnd));

        // Continue with messages for next database
        pos = dbIdEnd;
      }
      
      
      client.commitMessages.clear();
      
      
      // Grant the client all implicitly acquired write locks from creating blobs (they are preseved across transactions as sticky locks)
      // The following method doesn't have to check for any lock conflicts as no other client could possibly hold locks to not yet created blobs.
      for (auto& [dbId, locks] : implicitWriteLocks) {
        client.AcquireImplicitWriteLocks(dbId, locks);
      }


      // Allocate the response message with enough space to communicate all commit ids back to the client
      auto commitResponse = network::message::TransactionCommitResponse::Create(commitResults.size());
      auto writePos = commitResponse->begin();

      // Now we can apply all the database snapshots to the transient database state
      for (auto& [databaseId, commitResult] : commitResults) {
        writePos->dbId = databaseId;
        writePos->commitId = commitResult.ApplyToDatabase();
        ++writePos;
      }
      
      // Send reply to client      
      server->SendMessageToClient(client.id, std::move(commitResponse));

      // We committed all changes for this transaction and replied to the client,
      // Reset the transaction state and all locks held by the client, which may in turn trigger processing of outstanding reads
      // FIXME STICKY the client should be able to specify whether he wants to keep his locks at transaction commit (as sticky locks) or release them proactively
      AbortTransaction(client, false);
    } else {
      // Commit not successful -> return error code to the client and abort its transaction commit (and active transaction)
      server->SendMessageToClient(client.id, network::message::TransactionCommitResponse::CreateError(result));

      // Abort transaction and release all locks, which may in turn trigger processing of outstanding reads
      AbortTransactionCommit(client);
    }
  }
}

namespace {
  struct BlobLocationRange {
    BlobLocationRange() {}

    /** A range encompassing the whole segment
     */
    BlobLocationRange(segment_id segment) : begin(segment, 0, 0), end(segment+1, 0, 0) {}

    /** A range encompassing the whole cluster
     */
    BlobLocationRange(segment_id segment, cluster_id cluster) : begin(segment, cluster, 0), end(segment, cluster+1, 0) {}

    /** A range encompassing the specified blob
     */
    BlobLocationRange(segment_id segment, cluster_id cluster, blob_id blob) : begin(segment, cluster, blob), end(segment, cluster, blob+1) {}

    /** A range encompassing the specified blob range inside a cluster and segment
     */
    BlobLocationRange(segment_id segment, cluster_id cluster, blob_id blobBegin, blob_id blobEnd) : begin(segment, cluster, blobBegin), end(segment, cluster, blobEnd) {}

    BlobLocation begin, end; // regular [begin;end) interval with exclusive end
  };


  class BlobLocationRanges {
  public:
    /** True if any entry encompasses the given location
     */
    bool Encompasses(const BlobLocation& location) const {
      return std::any_of(ranges.begin(), ranges.end(), [&](const BlobLocationRange& range) { return range.begin <= location && location < range.end; });
    }

    /** True if any entry encompasses the whole passed range
     */
    bool Encompasses(const BlobLocationRange& checkRange) const {
      return std::any_of(ranges.begin(), ranges.end(), [&](const BlobLocationRange& range) { return range.begin <= checkRange.begin && range.end >= checkRange.end; });
    }

    void Enter(const BlobLocationRange& range) {
      ranges.push_back(range);
    }


    /** Constructs and enters the ranges for all implicitly created blobs when creating a new cluster
     */
    void EnterNewCluster(segment_id segmentId, cluster_id clusterId) {
      // First blob 0 is implicitly created
      ranges.push_back({ segmentId, clusterId, 0 });

      // Then all special blobs at the end of the cluster are also implicitly created
      BlobLocationRange range;
      range.begin = BlobLocation(segmentId, clusterId, constants::MaxBlobId + 1);
      range.end = BlobLocation(segmentId, clusterId + 1, 0);
      ranges.push_back(range);
    }

    /** Constructs and enters the ranges for all implicitly created blobs and the cluster
     */
    void EnterNewSegment(segment_id segmentId) {
      // Cluster 0 is implicitly created
      EnterNewCluster(segmentId, 0);

      // The special clusterids are also implicitly created
      BlobLocationRange range;
      range.begin = BlobLocation(segmentId, constants::MaxClusterId + 1, 0);
      range.end = BlobLocation(segmentId + 1, 0, 0);
      ranges.push_back(range);
    }

  private:
    std::vector<BlobLocationRange> ranges;
  };
}



network::message::TransactionCommitResponse::Result Server::ValidateCommitMessages(const blobs::server::Client& client, ImplicitLocks& implicitWriteLocks) const {
  using Result = network::message::TransactionCommitResponse::Result;

  // This vector is used to ensure that the commit messages are ordered by databases
  std::vector<Database*> touchedDatabases;

  for (auto& messagePtr : client.commitMessages) {
    auto& message = *messagePtr;
    auto database = client.GetDatabase(message.databaseId);
    if (!database) {
      return Result::DATABASE_NOT_OPENED;
    }

    // Ensure that databases are processed in order (i.e. first all messages for databaseA, then all messages for databaseB, not mixed)
    // This grouping is necessary to perform an efficient database snapshot update by simply passing it a contiguous range of messages, which
    // apply to the specified database.
    auto pos = std::find(touchedDatabases.begin(), touchedDatabases.end(), database);
    if (pos == touchedDatabases.end()) {
      // This database has not yet been processed
      touchedDatabases.push_back(database);
      implicitWriteLocks.push_back(std::make_pair(message.databaseId, std::vector<BlobLocation>{}));
    } else {
      // If the database was already mentioned in a previous commit message, then it must be the last mentioned one
      if (++pos != touchedDatabases.end()) {
        return Result::DATABASE_ORDER_VIOLATED;
      }
    }

    // Since we process databases in order we can track this database's write locks in the last entry
    auto& implicitDbWriteLocks = implicitWriteLocks.back().second;


    // In case the client created some blobs, this vector will be filled with ranges of created blobs
    BlobLocationRanges createdBlobs;

    for (auto pos = message.begin(), end = message.end(); pos != end; ++pos) {
      auto& location = *pos;
      if (!database->ClientOwnsWriteLock(client.id, location)) {
        // Client doesn't own a write lock for the committed blob. This is allowed if the client
        // created the blob in this transaction by acquring a write lock to the NextFreeBlobId blob and
        // updating the blob id there accordingly.

        if (!createdBlobs.Encompasses(location)) {
          // This blob has not been created in this transaction, which is not allowed.
          // This is an illegal commit.
          return Result::MISSING_WRITE_LOCK;
        } else {
          // The client holds a write lock, because he created this blob
          implicitDbWriteLocks.push_back(location);
        }
      }


      TODO("If cluster is SegmentDeleteId, then blob must be ClusterDeleteId");
      
      TODO("If cluster is SegmentDeleteId, then the whole segment must be write locked");
      TODO("If blob is ClusterDeleteId, then the whole cluster must be write locked");



      if (location.segment == constants::NextFreeSegmentId) {
        // The client has creatd one or more segments in the database
        if (location.cluster != constants::NextFreeClusterId || location.blob != constants::NextFreeBlobId) {
          // If segment is NextFreeSegmentId, then cluster MUST be NextFreeClusterId and blob MUST be NextFreeBlobId
          return Result::BLOB_DOES_NOT_EXIST;
        }


        auto committedContent = pos.ReadData();
        // Validate size of transmitted segment id blob
        if (committedContent.size() != sizeof(segment_id)) {
          // We expect exactly segment_id-bytes to be written, nothing more, nothing less!
          return Result::ILLEGAL_NEXT_FREE_SEGMENT_ID;
        }

        // Validate range of transmitted segment id. It cannot be smaller than the previous value.
        // An equal value is a bit weird, because the client decided to not create a new segment after all
        // The maximum value is MaxSegmentId+1, which is reached after the segment with id = MaxSegmentId has been created to mark a full database.
        auto newNextFreeId = *reinterpret_cast<const segment_id*>(committedContent.data());
        if (newNextFreeId < database->GetNextFreeSegmentId() || newNextFreeId > constants::MaxSegmentId + 1) {
          return Result::ILLEGAL_NEXT_FREE_SEGMENT_ID;
        }

        // Now construct the blob/cluster ranges for each segment created. We will NOT simply create one huge range for all segments 
        // created as this would allow the client to write into arbitrary blobs, which haven't been created according to the NextFreeBlobId entry
        for (segment_id segment = database->GetNextFreeSegmentId(); segment < newNextFreeId; ++segment) {
          createdBlobs.EnterNewSegment(segment);
        }

      } else if (location.cluster == constants::NextFreeClusterId) {
        // The client has creatd one or more clusters in the specified segment
        if (location.blob != constants::NextFreeBlobId) {
          // If cluster is NextFreeClusterId, then blob MUST be NextFreeBlobId
          return Result::BLOB_DOES_NOT_EXIST;
        }

        cluster_id nextFreeClusterId; // The current nextFreeClusterId of the segment

        if (auto segment = database->GetSegment(location.segment)) {
          // Creating a cluster in an already existing segment
          nextFreeClusterId = segment->GetNextFreeClusterId();
        } else if (createdBlobs.Encompasses(location)) {
          // This segment's (NextFreeClusterId,NextFreeBlobId) has been created in this transaction -> the segment does exist (just not in the database yet)
          nextFreeClusterId = 1; // The default nextFreeClusterId for newly created segments is 1
        } else {
          // This should actually not happen, because the client cannot acquire a lock in a not existing segment, right?
          return Result::SEGMENT_DOES_NOT_EXIST;
        }

        auto committedContent = pos.ReadData();
        // Validate size of transmitted cluster id blob
        if (committedContent.size() != sizeof(cluster_id)) {
          // We expect exactly cluster_id-bytes to be written, nothing more, nothing less!
          return Result::ILLEGAL_NEXT_FREE_CLUSTER_ID;
        }

        // Validate range of transmitted cluster id. It cannot be smaller than the previous value.
        // An equal value is a bit weird, because the client decided to not create a new cluster after all
        // The maximum value is MaxClusterId+1, which is reached after the cluster with id = MaxClusterId has been created to mark a full segment.
        auto newNextFreeClusterId = *reinterpret_cast<const cluster_id*>(committedContent.data());
        if (newNextFreeClusterId < nextFreeClusterId || newNextFreeClusterId > constants::MaxClusterId + 1) {
          return Result::ILLEGAL_NEXT_FREE_CLUSTER_ID;
        }


        // Now construct the blob ranges for each cluster created. We will NOT simply create one huge range for all clusters
        // created as this would allow the client to write into arbitrary blobs, which haven't been created according to the NextFreeBlobId entry
        for (cluster_id cluster = nextFreeClusterId; cluster < newNextFreeClusterId; ++cluster) {
          createdBlobs.EnterNewCluster(location.segment, cluster);
        }

      } else if (location.blob == constants::NextFreeBlobId) {
        // The client has created one or more new blobs in the specified cluster

        blob_id nextFreeBlobId; // The current next free blob id value

        if (createdBlobs.Encompasses(location)) {
          // The cluster and or segment have been created in this transaction so there is no point in performing the segment/cluster lookup
          // We can assume the nextFreeBlobId to have been 1 as the 0'th blob is always implicitly created
          nextFreeBlobId = 1;
        } else if (auto segment = database->GetSegment(location.segment)) {
          if (auto cluster = segment->GetCluster(location.cluster)) {
            nextFreeBlobId = cluster->GetNextFreeBlobId();
          } else {
            // This should actually not happen, because the client cannot acquire a lock in a not existing cluster, right?
            return Result::CLUSTER_DOES_NOT_EXIST;
          }
        } else {
          // This should actually not happen, because the client cannot acquire a lock in a not existing segment, right?
          return Result::SEGMENT_DOES_NOT_EXIST;
        }


        auto committedContent = pos.ReadData();

        // Validate size of transmitted blob id blob
        if (committedContent.size() != sizeof(blob_id)) {
          // We expect exactly blob_id-bytes to be written, nothing more, nothing less!
          return Result::ILLEGAL_NEXT_FREE_BLOB_ID;
        }

        // Validate range of transmitted blob id. It cannot be smaller than the previous value.
        // An equal value is a bit weird, because the client decided to not create a new blob after all
        // The maximum value is MaxBlobId+1, which is reached after the blob with id = MaxBlobId has been created to mark a full cluster.
        auto newNextFreeBlobId = *reinterpret_cast<const blob_id*>(committedContent.data());
        if (newNextFreeBlobId < nextFreeBlobId || newNextFreeBlobId > constants::MaxBlobId + 1) {
          return Result::ILLEGAL_NEXT_FREE_BLOB_ID;
        }

        // Add the range to the list of created blobs to not fail our lock check for newly created blobs
        createdBlobs.Enter(BlobLocationRange(location.segment, location.cluster, nextFreeBlobId, newNextFreeBlobId));
      }
    }
  }


  return Result::SUCCESS;
}

void Server::AbortTransactionCommit(blobs::server::Client& client) {
  client.commitMessages.clear();
  // A failed transaction commit indicates some kind of client side error, so we will release all locks now to reduce the 
  // impact this faulty client can have on other clients connected to the server.
  AbortTransaction(client, true); 
}



bool Server::TryHandleBlobsRead(const network::message::BlobsRead& message) {
  auto& client = server::Client::Get(message.clientId);
  auto database = client.GetDatabase(message.databaseId);
  if (!database) {
    SendMessageToClient(message.clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::DATBASE_NOT_OPENED));
    return true;
  }

  // Client has to explicitly start a transaction before being able to read anything
  if (!client.IsInsideTransaction()) {
    SendMessageToClient(message.clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::NO_TRANSACTION_IN_PROGRESS));
    return true;
  }

  if (message.nBlobsRequested == 1) {
    // Fast path: at most 1 blob needs to be sent to the client
    auto& requestedBlob = *message.begin();
    auto blob = database->GetBlob(requestedBlob);
    if (!blob) {
      SendMessageToClient(message.clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST));
      return true;
    }

    if (client.AcquireLocks(message)) {
      // Locks successfully acquired (no conflicts) -> send response
      if (requestedBlob.ifCommitIdHigher >= blob->commitId || message.lockMode == network::message::BlobsRead::LockMode::Delete) {
        // - The client has the current version of the blob 
        // - Or the client requested the write locks only for deletion of the blobs
        // --> In both cases we can simply send an empty response
        SendMessageToClient(message.clientId, network::message::BlobsReadResponse::Create(0, 0));
      } else {
        // Client's blob is not up to date -> send the server's current version
        auto blobContent = blob->ReadContent();
        auto response = network::message::BlobsReadResponse::Create(blobContent.size());
        response->begin().SetBlob(requestedBlob, blob->commitId, blobContent.data(), static_cast<blob_size>(blobContent.size()));
        SendMessageToClient(message.clientId, std::move(response));
      }
      return true; // messages fully processed
    } else {
      // We have conflicting locks
      return false;
    }
  } else {
    TODO("Handle multi blob requests");
    assert(false);
    return true;
  }
}

void Server::LogMessage(const network::message::Message& message) {
  BLOBS_LOG_DEBUG("Client[" << message.clientId << "]: " << message);
}



void Server::AbortTransaction(blobs::server::Client& client, bool releaseAllLocks) {
  if (client.AbortTransaction(releaseAllLocks)) {
    // We actually aborted a running transaction.

    // Now try to process any outstanding reads
    ClientTransactionEnded(client);
  }
  // else simply ignore transaction abort if the client had no transaction running... malicious or confused client?
}



void Server::TryProcessQueuedReads(blobs::server::Database& database) {
  for (auto pos = database.queuedReads.begin(), end = database.queuedReads.end(); pos != end;) {
    auto& readBlobsMessage = *pos;
    if (TryHandleBlobsRead(*readBlobsMessage)) {
      // Message processed, we can remove it from the queue
      database.queuedReads.erase(pos++);
    } else {
      // Check next queued message in queue
      ++pos;
    }
  }
}


void Server::ClientTransactionEnded(const blobs::server::Client& client) {
  for (database_id dbId = 0, maxId = client.GetMaxDatabaseId(); dbId <= maxId; ++dbId) {
    if (auto database = client.GetDatabase(dbId)) {
      // Database is actually open -> process all queued reads
      TryProcessQueuedReads(*database);
    }
  }
}





Server::MessageReceiveHandler::MessageReceiveHandler(Server& server) : server(server) {}

void Server::MessageReceiveHandler::HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) {
  server.ProcessReceivedMessages();
}


}}


