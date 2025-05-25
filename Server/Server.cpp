#include "pch.hpp"
#include "Server.hpp"
#include "Client.hpp"

#include <iostream>

namespace blobs {
namespace server {

Server::Server(int port) : server(port) {}


void Server::ServerMain() {
  TODO("Figure out when to shutdown the server");

  while (true) {
    auto message = server.AwaitMessage();
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

      case network::message::Type::TransactionAbort:
        HandleTransactionAbort(message.Cast<network::message::TransactionAbort>());
        break;

      case network::message::Type::TransactionCommit:
        HandleTransactionCommit(message.Cast<network::message::TransactionCommit>());
        break;


        //TODO: handle other messages

      default:
        std::cerr << "[ERR] Server received message of unexpected type " << message->type << "(" << static_cast<int>(message->type) << ") from client " << message->clientId << "\n";
        break;
    }
  }
}



void Server::HandleConnectionOpened(network::MessagePointer_T<network::message::ConnectionOpened> message) {
  // New client connected (initialize logical client data)
  Client::ClientConnected(message->clientId);
}

void Server::HandleConnectionClosed(network::MessagePointer_T<network::message::ConnectionClosed> message) {
  // A client closed the connection
  TODO("Remove from client map, release all held locks, release database references and close database if this was the last one");
}

void Server::HandleDatabaseOpen(network::MessagePointer_T<network::message::DatabaseOpen> message) {
  // OpenDB request
  auto dbName = message->GetDatabaseName();

  TODO("Handle error in case database is not found");
  auto& db = server::Database::Get(dbName);
  auto& client = server::Client::Get(message->clientId);

  try {
    server.SendDatabaseOpenResponse(client.id, network::message::DatabaseOpenResponse::Result::SUCCESS, client.OpenDatabase(db));
  } catch (std::exception&) {
    server.SendDatabaseOpenResponse(client.id, network::message::DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN, 0);
  }
}


void Server::HandleDatabaseClose(network::MessagePointer_T<network::message::DatabaseClose> message) {
  TODO("Get the database to close");
  // Confirm closing the database to the client by replying with the same DatabaseClose message.
  // Here we simply post the same message object into the client send buffer to avoid the unnecessary reallocation
  auto clientId = message->clientId; // assign to local variable here, because we will move the message as a whole
  server.SendMessageToClient(clientId, std::move(message));

  TODO("Don't allow db close if there is a transaction running")
  TODO("If this was the last database, then we can also just close the connection")
}


void Server::HandleBlobsRead(network::MessagePointer_T<network::message::BlobsRead> message) {
  if (!TryHandleBlobsRead(*message)) {
    // Cannot immediately reply to message because of locking conflicts
    auto& client = server::Client::Get(message->clientId);
    auto database = client.GetDatabase(message->databaseId);
    assert(database); // <- TryHandleBlobsRead() would have returned true otherwise

    // Queue this message to the databse to be processed as soon as the conflicting locks are released
    if (!database->QueueReadCheckDeadlock(std::move(message))) {
      // Deadlock detected -> cannot enqueue message
      client.AbortTransaction(); // abort the client's current transaction
      server.SendMessageToClient(client.id, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::DEADLOCK));
      TODO("Also check whether any outstanding reads can be completed now that the client has released his previously held locks");
    }
  }
}


void Server::HandleTransactionAbort(network::MessagePointer_T<network::message::TransactionAbort> message) {
  AbortTransaction(server::Client::Get(message->clientId));
}


void Server::HandleTransactionCommit(network::MessagePointer_T<network::message::TransactionCommit> message) {
  // For now only handle simple case with a single commit message
  auto& client = server::Client::Get(message->clientId);

  // To handle multi message commits we push the messages into a commit message vector, which will also
  // make the server reject any other messages for this client.
  client.commitMessages.push_back(std::move(message));

  if (!client.commitMessages.back()->hasFollowMessage) {
    // This is the final commit message in a list of commit messages -> validate the commit.
    // We must only validate the commit messages AFTER we received every commit message of a multipart commit, because the client
    // does not expect any response to partial commit messages, as this would only slow down the commit process when commiting to multiple
    // databases at once.

    auto result = ValidateCommitMessages(client);
    if (result == network::message::TransactionCommitResponse::Result::SUCCESS) {
      // Commit messages are valid
      TODO("Post the commit messages into the transaction log");
      TODO("Apply changes to the transient database state");
      TODO("Update commitId members in all affected blobs... also the NextFreeBlobId blob!");


      TODO("Determine the commit id (which is different per database... so we cannot just return one commit id in this message)");
      commit_id commitId = 0;
      server.SendMessageToClient(client.id, network::message::TransactionCommitResponse::Create(commitId));
    } else {
      // Commit not successful -> return error code to the client and abort its transaction commit (and active transaction)
      server.SendMessageToClient(client.id, network::message::TransactionCommitResponse::CreateError(result));
      AbortTransactionCommit(client);
    }
  }
}

namespace {
  struct BlobLocationRange {
    BlobLocation begin, end; // regular [begin;end) interval with exclusive end
  };
}



network::message::TransactionCommitResponse::Result Server::ValidateCommitMessages(const blobs::server::Client& client) const {
  using Result = network::message::TransactionCommitResponse::Result;

  for (auto& messagePtr : client.commitMessages) {
    auto& message = *messagePtr;
    auto database = client.GetDatabase(message.databaseId);
    if (!database) {
      return Result::DATBASE_NOT_OPENED;
    }

    // In case the client created some blobs, this vector will be filled with ranges of created blobs
    std::vector<BlobLocationRange> createdBlobs;

    for (auto pos = message.begin(), end = message.end(); pos != end; ++pos) {
      auto& location = *pos;
      if (!database->ClientOwnsWriteLock(client.id, location)) {
        // Client doesn't own a write lock for the committed blob. This is allowed if the client
        // created the blob in this transaction by acquring a write lock to the NextFreeBlobId blob and
        // updating the blob id there accordingly.

        if (!std::any_of(createdBlobs.begin(), createdBlobs.end(), [&](const BlobLocationRange& range) { return range.begin <= location && location < range.end; })) {
          // This blob has not been created in this transaction, which is not allowed.
          // This is an illegal commit.
          return Result::MISSING_WRITE_LOCK;
        }
      }


      TODO("Check for NextFreeSegmentId and NextFreeClusterId first!");

      if (location.blob == constants::NextFreeBlobId) {
        // The client has created one or more new blobs in the specified cluster
        if (auto segment = database->GetSegment(location.segment)) {
          if (auto cluster = segment->GetCluster(location.cluster)) {
            BlobLocationRange createRange;
            createRange.begin = location; // slicing is expected here
            createRange.begin.blob = cluster->GetNextFreeBlobId();

            auto committedContent = pos.ReadData();
            createRange.end = location; // slicing is expected here
            
            // Validate size of transmitted blob id blob
            if (committedContent.size() != sizeof(blob_id)) {
              // We expect exactly blob_id-bytes to be written, nothing more, nothing less!
              return Result::ILLEGAL_NEXT_FREE_BLOB_ID;
            }

            // Validate range of transmitted blob id. It cannot be smaller than the previous value.
            // An equal value is a bit weird, because the client decided to not create a new blob after all
            // The maximum value is MaxBlobId+1, which is reached after the blob with id = MaxBlobId has been created to mark a full cluster.
            auto newNextFreeId = *reinterpret_cast<const blob_id*>(committedContent.data());
            if (newNextFreeId < cluster->GetNextFreeBlobId() || newNextFreeId > constants::MaxBlobId + 1) {
              return Result::ILLEGAL_NEXT_FREE_BLOB_ID;
            }

            createRange.end.blob = newNextFreeId;

            // Add the range to the list of created blobs to not fail our lock check for newly created blobs
            createdBlobs.push_back(createRange);
          } else {
            // This should actually not happen, because the client cannot acquire a lock in a not existing cluster, right?
            return Result::CLUSTER_DOES_NOT_EXIST;
          }
        } else {
          // This should actually not happen, because the client cannot acquire a lock in a not existing segment, right?
          return Result::SEGMENT_DOES_NOT_EXIST;
        }
      }
    }
  }


  return Result::SUCCESS;
}

void Server::AbortTransactionCommit(blobs::server::Client& client) {
  client.commitMessages.clear();
  AbortTransaction(client);
}



bool Server::TryHandleBlobsRead(const network::message::BlobsRead& message) {
  auto& client = server::Client::Get(message.clientId);
  auto database = client.GetDatabase(message.databaseId);
  if (!database) {
    server.SendMessageToClient(message.clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::DATBASE_NOT_OPENED));
    return true;
  }

  if (message.nBlobsRequested == 1) {
    // Fast path: at most 1 blob needs to be sent to the client
    auto& requestedBlob = *message.begin();
    auto blob = database->GetBlob(requestedBlob);
    if (!blob) {
      server.SendMessageToClient(message.clientId, network::message::BlobsReadResponse::CreateError(network::message::BlobsReadResponse::Result::BLOB_DOES_NOT_EXIST));
      return true;
    }

    if (client.AcquireLocks(message)) {
      // Locks successfully acquired (no conflicts) -> send response
      if (requestedBlob.ifCommitIdHigher >= blob->commitId || message.lockMode == network::message::BlobsRead::LockMode::Delete) {
        // - The client has the current version of the blob 
        // - Or the client requested the write locks only for deletion of the blobs
        // --> In both cases we can simply send an empty response
        server.SendMessageToClient(message.clientId, network::message::BlobsReadResponse::Create(0, 0));
      } else {
        // Client's blob is not up to date -> send the server's current version
        auto response = network::message::BlobsReadResponse::Create(blob->data.size());
        response->begin().SetBlob(requestedBlob, blob->commitId, blob->data.data(), static_cast<blob_size>(blob->data.size()));
        server.SendMessageToClient(message.clientId, std::move(response));
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
  FIXME("This should be disabled in production code as it probably slows down the server as the windows console is known to be slow");
  std::cout << "Client[" << message.clientId << "]: " << message << "\n";
}



void Server::AbortTransaction(blobs::server::Client& client) {
  if (client.AbortTransaction()) {
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


}}


