#include "pch.hpp"
#include "include/server/Client.hpp"


namespace blobs {
namespace server {

std::unordered_map<client_id, Client> Client::clients;

Client::Client(client_id id) : id(id), transaction(Transaction::None) {}


void Client::ClientConnected(client_id id) {
  auto inserted = clients.emplace(id, Client(id)).second;
  TODO("We should return some kind of error code and cancel the connection if there is already a client with this id");
  assert(inserted);
}


Client& Client::Get(client_id id) {
  auto pos = clients.find(id);
  assert(pos != clients.end());
  return pos->second;
}

bool Client::HasDatabaseOpened(Database& db) const {
  return std::any_of(openDatabases.begin(), openDatabases.end(), [&](const DatabaseLocks& entry) { return entry.database == &db; });
}


database_id Client::OpenDatabase(Database& db) {

  // First find a nullptr to use inside 
  auto pos = std::find_if(openDatabases.begin(), openDatabases.end(), [](const DatabaseLocks& entry) { return entry.database == nullptr; });
  if (pos != openDatabases.end()) {
    // simply reuse the free slot
    pos->database = &db;
    return static_cast<database_id>(std::distance(openDatabases.begin(), pos));
  }

  // We must grow the databases list (unless we reached the limit)
  if (openDatabases.size() == std::numeric_limits<database_id>::max() - 1) {
    // If we were to grow it now, the index would be MAX+1, which is not a valid db index anymore
    throw std::exception("Too many databases open, cannot open another database");
  }

  openDatabases.push_back({ &db });
  return static_cast<database_id>(openDatabases.size() - 1);
}


Database* Client::GetDatabase(database_id id) const {
  return (id < openDatabases.size()) ? openDatabases[id].database : nullptr;
}

database_id Client::GetMaxDatabaseId() const {
  return openDatabases.empty() ? 0 : static_cast<database_id>(openDatabases.size() - 1);
}

bool Client::AbortTransaction() {
  if (transaction != Transaction::None) { // Nothing to do if no transaction is in progress

    // Release the locks held in all databases
    for (auto& dbEntry : openDatabases) {
      if (dbEntry.database) {
        // Release all locks held by this client and remove any queued up read operation
        dbEntry.database->AbortClientTransaction(id, dbEntry.locks);
        dbEntry.locks.clear();
      }
    }

    // Mark the transaction as gone
    transaction = Transaction::None;
    return true;
  }

  return false;
}



bool Client::AcquireLocks(const network::message::BlobsRead& message) {
  // The caller should ensure this is not called with an invalid database id
  assert(openDatabases.size() > message.databaseId && openDatabases[message.databaseId].database != nullptr);
  auto& openDb = openDatabases[message.databaseId]; 

  if (openDb.database->AcquireLocks(message)) {
    // Implicitly start a transaction upon acquiring the first lock
    if (transaction == Transaction::None) {
      transaction = Transaction::Write;
    }

    // All locks could be acquired -> enter them into the database entry to mark down all our lock locations
    for (auto& location : message) {
      auto pos = std::find(openDb.locks.begin(), openDb.locks.end(), location);
      if (pos == openDb.locks.end()) {
        // A new lock
        openDb.locks.push_back(location);
      }
    }
    
    return true;
  }
   
  return false;
}


bool Client::CommitInProcess() const {
  return !commitMessages.empty();
}


}}