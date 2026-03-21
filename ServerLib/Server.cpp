#include "pch.hpp"
#include "include/server/Server.hpp"
#include "include/server/Client.hpp"
#include "include/server/Logging.hpp"
#include "include/server/LockUtil.hpp"

#include <network/Factory.hpp>
#include <common/Encoding.hpp>
#include <common/Paths.hpp>

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

void Server::ServerMain(std::optional<std::wstring_view> dbRootDir) {
  BLOBS_LOG_DEBUG("Staring Server::ServerMain()");
  BLOBS_LOG_DEBUG("Current directory: " << encoding::ToUTF8(Paths::GetWorkingDirectory()));

  TODO("Write automated tests ensuring that the db root works correctly (case-insensitivity / path restriction)");
  if (dbRootDir) {
    this->dbRootDir = Paths::ResolvePath(*dbRootDir);
    BLOBS_LOG_DEBUG("Database root dir: " << encoding::ToUTF8(*this->dbRootDir));
    // Create the root directory in case it doesn't exist yet
    Paths::MakeDirs(*this->dbRootDir);
  } else {
    BLOBS_LOG_DEBUG("Database root dir: [disabled]");
  }

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



std::optional<std::wstring> Server::GetResolvedDatabasePath(std::string_view databaseName) const {
  if (dbRootDir) {
    return Paths::ResolvePath(*dbRootDir, encoding::ToUTF16(databaseName));
  } else {
    return Paths::ResolvePath(encoding::ToUTF16(databaseName));
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

  if (!message->keepStickyLocks) {
    // Release all still held locks
    client.ReleaseAllLocks();
  }


  TODO("Support a MVCC transaction in the future by fixing a snapshot of the database");
  client.BeginTransaction();

  SendMessageToClient(client.id, client.ConstructTransactionBeginResponse());
}


void Server::HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message) {
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


      // Allocate the response message with enough space to communicate all commit ids back to the client
      auto commitResponse = network::message::TransactionCommitResponse::Create(commitResults.size());
      auto writePos = commitResponse->begin();

      // Now we can apply all the database snapshots to the transient database state
      for (auto& [databaseId, commitResult] : commitResults) {
        writePos->dbId = databaseId;
        writePos->commitId = commitResult.ApplyToDatabase();
        ++writePos;
      }

      // The commit has now been successfully applied to the database file and write was successful.

      // Grant the client all implicitly acquired write locks from creating blobs (they are preseved across transactions as sticky locks)
      // The following method doesn't have to check for any lock conflicts as no other client could possibly hold locks to not yet created blobs.
      for (auto& [dbId, locks] : implicitWriteLocks) {
        client.AcquireImplicitWriteLocks(dbId, locks);
      }

      // Now release all locks in any deleted blobs,clusters,segments as they now refer to deleted resources.
      // The client side is expected to know that these locks are implicitly released during the commit.
      for (auto& [databaseId, commitResult] : commitResults) {
        client.ReleaseDeletedLocks(databaseId, commitResult.deleted);
      }
      
      // Send reply to client      
      server->SendMessageToClient(client.id, std::move(commitResponse));

      // We committed all changes for this transaction and replied to the client,
      // Reset the transaction state and all locks held by the client, which may in turn trigger processing of outstanding reads
      AbortTransaction(client, false);
    } else {
      // Commit not successful -> return error code to the client and abort its transaction commit (and active transaction)
      server->SendMessageToClient(client.id, network::message::TransactionCommitResponse::CreateError(result));

      // Abort transaction and release all locks, which may in turn trigger processing of outstanding reads
      AbortTransactionCommit(client);
    }
  }
}



network::message::TransactionCommitResponse::Result Server::ValidateCommitMessages(const blobs::server::Client& client, ImplicitLocks& implicitWriteLocks) const {
  using Result = network::message::TransactionCommitResponse::Result;

  // This vector is used to ensure that the commit messages are ordered by databases
  // We will collect the database_ids mentioned in the messages in the same order to process the commits by database
  std::vector<database_id> touchedDatabases;
  for (auto& messagePtr : client.commitMessages) {
    auto& message = *messagePtr;

    // Ensure that databases are processed in order (i.e. first all messages for databaseA, then all messages for databaseB, not mixed)
    // This grouping is necessary to perform an efficient database snapshot update by simply passing it a contiguous range of messages, which
    // apply to the specified database.
    auto pos = std::find(touchedDatabases.begin(), touchedDatabases.end(), message.databaseId);
    if (pos == touchedDatabases.end()) {
      // This database has not yet been processed
      touchedDatabases.push_back(message.databaseId);
    } else {
      // If the database was already mentioned in a previous commit message, then it must be the last mentioned one
      if (++pos != touchedDatabases.end()) {
        return Result::DATABASE_ORDER_VIOLATED;
      }
    }
  }


  auto messagesPos = client.commitMessages.begin();
  auto messagesEnd = client.commitMessages.end();

  for (auto databaseId : touchedDatabases) {
    auto database = client.GetDatabase(databaseId);
    if (!database) {
      return Result::DATABASE_NOT_OPENED;
    }

    // In case the client created some blobs, this vector will be filled with ranges of created blobs
    // This range of created blobs is tracked per database
    BlobLocationRanges createdBlobs;

    // Track implicitly granted write locks for this database
    auto& implicitDbWriteLocks = implicitWriteLocks.emplace_back(databaseId, std::vector<BlobLocation>{}).second;


    // Process all messages for this database id
    for (;  messagesPos != messagesEnd && (*messagesPos)->databaseId == databaseId; ++messagesPos) {
      auto& message = **messagesPos;


      // Process all touched blobs for this message
      for (auto pos = message.begin(), end = message.end(); pos != end; ++pos) {
        auto& location = *pos;

        // Use AllLocksForLocation() to also handle the case of DeleteClusterId, which requires the client
        // to hold locks on the whole cluster.
        if (!AllLocksForLocation(*database, location, [&](const BlobLocation& location) { return database->ClientOwnsWriteLock(client.id, location) || createdBlobs.Encompasses(location); }, &createdBlobs)) {
          // If the client doesn't own a write lock for a location we want to write to 
          // AND the client doesn't own the write lock implicitly by creating the blob/cluster/segment, then 
          // the client is missing the required lock for this commit and this is an illegal commit.
          return Result::MISSING_WRITE_LOCK;
        }


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

          if (auto segment = database->GetLoadedSegment(location.segment)) {
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
          } else if (auto segment = database->GetLoadedSegment(location.segment)) {
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

    // We finished processing all messages for this database
    // Now acquire implicit write locks for all implicitly created blobs/clusters/segments
    for (auto& range : createdBlobs) {
      if (range.IsCreatedSegment()) {
        // This range represents a newly created segment
        auto segment = range.begin.segment;

        // Implicit write lock on cluster creation
        BlobLocation location(segment, constants::NextFreeClusterId, constants::NextFreeBlobId);
        if (!database->ClientOwnsWriteLock(client.id, location)) { 
          implicitDbWriteLocks.push_back(location);
        }

        // Implicit write lock on segment deletion
        location = BlobLocation(segment, constants::SegmentDeleteId, constants::ClusterDeleteId);
        if (!database->ClientOwnsWriteLock(client.id, location)) {
          implicitDbWriteLocks.push_back(location);
        }

        // Implicit write lock on the segment's cluster list
        location = BlobLocation(segment, constants::ClusterListId, constants::BlobListId);
        if (!database->ClientOwnsWriteLock(client.id, location)) {
          implicitDbWriteLocks.push_back(location);
        }
        
      } else if (range.IsCreatedCluster()) {
        auto segment = range.begin.segment;
        auto cluster = range.begin.cluster;

        // Implicit write lock on blob creation
        BlobLocation location(segment, cluster, constants::NextFreeBlobId);
        if (!database->ClientOwnsWriteLock(client.id, location)) {
          implicitDbWriteLocks.push_back(location);
        }

        // Implicit write lock on cluster deletion
        location = BlobLocation(segment, cluster, constants::ClusterDeleteId);
        if (!database->ClientOwnsWriteLock(client.id, location)) {
          implicitDbWriteLocks.push_back(location);
        }

        // Implicit write lock on the list of blobs in the cluster
        location = BlobLocation(segment, cluster, constants::BlobListId);
        if (!database->ClientOwnsWriteLock(client.id, location)) {
          implicitDbWriteLocks.push_back(location);
        }

      } else {
        // one or more regular created blobs
        auto segment = range.begin.segment;
        auto cluster = range.begin.cluster;

        for (auto blob = range.begin.blob; blob != range.end.blob; ++blob) {
          BlobLocation location(segment, cluster, blob);
          if (!database->ClientOwnsWriteLock(client.id, location)) {
            implicitDbWriteLocks.push_back(location);
          }
        }
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
  // unless the client performs a dirty read, which is also allowed outside of a transaction as it will never
  // set any locks because it doesn't have to ensure consistency with any other reads.
  if (!message.IsDirtyRead() && !client.IsInsideTransaction()) {
    SendMessageToClient(message.clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::NO_TRANSACTION_IN_PROGRESS));
    return true;
  }

  if (message.nBlobsRequested == 1) {
    // Fast path: at most 1 blob needs to be sent to the client
    auto& requestedBlob = *message.begin();


    // Handle delete segment request  (this must always be a delete lock)
    if (requestedBlob.cluster == constants::SegmentDeleteId && requestedBlob.blob == constants::ClusterDeleteId && message.lockMode == network::message::BlobsRead::LockMode::Delete) {
      return TryHandleDeleteSegmentId(client, message);
    }

    // Handle delete cluster request (this must always be a delete lock)
    if (requestedBlob.blob == constants::ClusterDeleteId && message.lockMode == network::message::BlobsRead::LockMode::Delete) {
      return TryHandleDeleteClusterId(client, message);
    }

    // Handle requests for querying the list of all blobs, clusters, segments
    // We cannot use the default implementation because these too are just artificial blobs that don't exist in the database and are calculated on demand.
    if (requestedBlob.blob == constants::BlobListId) {
      if (requestedBlob.cluster == constants::ClusterListId) {
        if (requestedBlob.segment == constants::SegmentListId) {
          return TryHandleSegmentListId(client, message);
        }
        return TryHandleClusterListId(client, message);
      }
      return TryHandleBlobListId(client, message);
    }

    auto blob = database->GetLoadedBlob(requestedBlob);
    if (!blob) {
      SendMessageToClient(message.clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST));
      return true;
    }

    // The client does not need to acquire any locks if the client requested a dirty read
    if (message.IsDirtyRead() || client.AcquireLocks(message)) {
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
    // For this we would need to first check all resources whether they exist
    // Then check whether we can acquire all locks
    // Then acquire all locks
    // Then build the response from the results
    TODO("Handle multi blob requests");
    assert(false);
    return true;
  }
}


bool Server::TryHandleDeleteSegmentId(blobs::server::Client& client, const network::message::BlobsRead& message) {
  FIXME("This method must be split into mulitple parts if we ever want to support ReadBlobs requests with multiple blob ids");
  assert(message.nBlobsRequested == 1);
  auto& location = *message.begin();


  auto database = client.GetDatabase(message.databaseId);
  assert(database); // should have been checked by the caller

  if (!database->GetLoadedSegment(location.segment)) {
    // Segment do not exist
    SendMessageToClient(client.id, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::SEGMENT_DOES_NOT_EXIST));
    return true;
  }


  // Now acquire locks for all blobs inside the cluster including the artificial ones (NextFreeBlobId and DeleteClusterId)
  if (client.AcquireLocks(message)) {
    // Now we can reply with an empty response (delete lock must have been set in the message)
    assert(message.lockMode == network::message::BlobsRead::LockMode::Delete);
    SendMessageToClient(message.clientId, network::message::BlobsReadResponse::Create(0, 0));
    return true;
  }

  return false;
}



bool Server::TryHandleDeleteClusterId(blobs::server::Client& client, const network::message::BlobsRead& message) {
  FIXME("This method must be split into mulitple parts if we ever want to support ReadBlobs requests with multiple blob ids");
  assert(message.nBlobsRequested == 1);
  auto& location = *message.begin();
  

  auto database = client.GetDatabase(message.databaseId);
  assert(database); // should have been checked by the caller

  if (!database->GetLoadedCluster(location.segment, location.cluster)) {
    // Cluster or segment do not exist
    SendMessageToClient(client.id, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::CLUSTER_DOES_NOT_EXIST));
    return true;
  }
  

  // Now acquire locks for all blobs inside the cluster including the artificial ones (NextFreeBlobId and DeleteClusterId)
  if (client.AcquireLocks(message)) {
    // Now we can reply with an empty response (delete lock must have been set in the message)
    assert(message.lockMode == network::message::BlobsRead::LockMode::Delete);
    SendMessageToClient(message.clientId, network::message::BlobsReadResponse::Create(0, 0));
    return true;
  }
  
  return false;
}


namespace{
  template<typename T> using Range = std::pair<T, T>;

  /** A small helper function for converting the list of all blob,cluster,segment ids into a list of contiguous ranges
   */
  template<typename flat_map_iterator>
  std::vector<Range<typename std::iterator_traits<flat_map_iterator>::value_type::first_type>> intoIdRanges(flat_map_iterator begin, flat_map_iterator end) {
    using id_type = typename std::iterator_traits<flat_map_iterator>::value_type::first_type;
    std::vector<Range<id_type>> ranges;

    if (begin != end) {
      Range<id_type> currentRange { begin->first, begin->first+1 };
      while (++begin != end) {
        if (begin->first == currentRange.second) {
          // same contiguous range
          ++currentRange.second;
        } else {
          // a hole in the numbering -> start new range
          ranges.push_back(currentRange);
          currentRange = { begin->first, begin->first+1 };
        }
      }
      // enter the last range
      ranges.push_back(currentRange);
    }

    return ranges;
  }
}


bool Server::TryHandleBlobListId(blobs::server::Client& client, const network::message::BlobsRead& message) {
  FIXME("This method must be split into mulitple parts if we ever want to support ReadBlobs requests with multiple blob ids");
  assert(message.nBlobsRequested == 1);
  auto& location = *message.begin();

  auto database = client.GetDatabase(message.databaseId);
  assert(database); // should have been checked by the caller

  auto cluster = database->GetLoadedCluster(location.segment, location.cluster);
  if (!cluster) {
    // Segment do not exist
    SendMessageToClient(client.id, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::SEGMENT_DOES_NOT_EXIST));
    return true;
  }

  // Now acquire locks for the cluster id list
  if (message.IsDirtyRead() || client.AcquireLocks(message)) {
    if (location.ifCommitIdHigher >= cluster->commitId || message.lockMode == network::message::BlobsRead::LockMode::Delete) {
      // - The client has the current version of the list 
      // - Or the client requested the write locks only for synchronization of blob deletion/creation
      SendMessageToClient(client.id, network::message::BlobsReadResponse::Create(0, 0));
      return true;
    }

    // Now construct the cluster ranges and create a blobs response message for it
    auto ranges = intoIdRanges(cluster->begin(), cluster->end());
    auto byteSize = ranges.size() * sizeof(decltype(ranges)::value_type);

    auto response = network::message::BlobsReadResponse::Create(byteSize);
    response->begin().SetBlob(location, database->GetCommitId(), ranges.data(), byteSize);
    SendMessageToClient(message.clientId, std::move(response));
    return true;
  }

  return false;
}

bool Server::TryHandleClusterListId(blobs::server::Client& client, const network::message::BlobsRead& message) {
  FIXME("This method must be split into mulitple parts if we ever want to support ReadBlobs requests with multiple blob ids");
  assert(message.nBlobsRequested == 1);
  auto& location = *message.begin();

  auto database = client.GetDatabase(message.databaseId);
  assert(database); // should have been checked by the caller

  auto segment = database->GetLoadedSegment(location.segment);
  if (!segment) {
    // Segment do not exist
    SendMessageToClient(client.id, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::SEGMENT_DOES_NOT_EXIST));
    return true;
  }

  // Now acquire locks for the cluster id list
  if (message.IsDirtyRead() || client.AcquireLocks(message)) {
    if (location.ifCommitIdHigher >= segment->commitId || message.lockMode == network::message::BlobsRead::LockMode::Delete) {
      // - The client has the current version of the list
      // - Or the client requested the write locks only for synchronization of cluster deletion/creation
      SendMessageToClient(client.id, network::message::BlobsReadResponse::Create(0, 0));
      return true;
    }

    // Now construct the cluster ranges and create a blobs response message for it
    auto ranges = intoIdRanges(segment->begin(), segment->end());
    auto byteSize = ranges.size() * sizeof(decltype(ranges)::value_type);

    auto response = network::message::BlobsReadResponse::Create(byteSize);
    response->begin().SetBlob(location, database->GetCommitId(), ranges.data(), byteSize);
    SendMessageToClient(message.clientId, std::move(response));
    return true;
  }

  return false;
}

bool Server::TryHandleSegmentListId(blobs::server::Client& client, const network::message::BlobsRead& message) {
  FIXME("This method must be split into mulitple parts if we ever want to support ReadBlobs requests with multiple blob ids");
  assert(message.nBlobsRequested == 1);
  auto& location = *message.begin();

  auto database = client.GetDatabase(message.databaseId);
  assert(database); // should have been checked by the caller

  // Now acquire locks for the segment id list
  if (message.IsDirtyRead() || client.AcquireLocks(message)) {
    if (location.ifCommitIdHigher >= database->GetCommitId() || message.lockMode == network::message::BlobsRead::LockMode::Delete) {
      // - The client has the current version of the list
      // - Or the client requested the write locks only for synchronization of segment deletion/creation
      SendMessageToClient(client.id, network::message::BlobsReadResponse::Create(0, 0));
      return true;
    }

    // Now construct the segment ranges and create a blobs response message for it
    auto ranges = intoIdRanges(database->begin(), database->end());
    auto byteSize = ranges.size() * sizeof(decltype(ranges)::value_type);
    
    auto response = network::message::BlobsReadResponse::Create(byteSize);
    response->begin().SetBlob(location, database->GetCommitId(), ranges.data(), byteSize);
    SendMessageToClient(message.clientId, std::move(response));
    return true;
  }

  return false;
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


