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
  TODO("Increment database use count");

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


bool Client::CloseDatabase(database_id id) {
  if (auto database = GetDatabase(id)) {
    // database is actually opened
    assert(openDatabases[id].locks.empty()); // since no transaction should be running
    openDatabases[id].database = nullptr;


    // Decrement databse use count to cleanup the transiently held data structures if the database isn't needed anymore
    database->Release();

    return true; // database actually closed
  }

  return false;
}


Database* Client::GetDatabase(database_id id) const {
  return (id < openDatabases.size()) ? openDatabases[id].database : nullptr;
}


std::optional<database_id> Client::LookupDatabase(const Database& database) const {
  auto pos = std::find_if(openDatabases.begin(), openDatabases.end(), [&database](const DatabaseLocks& dbState) { return dbState.database == &database; });
  return pos != openDatabases.end() ? std::optional<database_id>(static_cast<database_id>(std::distance(openDatabases.begin(), pos))) : std::nullopt;
}

uint32_t Client::GetOpenDatabaseCount() const {
  uint32_t databases = 0;
  for (auto& entry : openDatabases) {
    if (entry.database) {
      ++databases;
    }
  }
  return databases;
}


database_id Client::GetMaxDatabaseId() const {
  return openDatabases.empty() ? 0 : static_cast<database_id>(openDatabases.size() - 1);
}

void Client::BeginTransaction() {
  TODO("Accept a parameter to determine the kind of transaction");
  transaction = Transaction::Write;


  //FIXME STICKY Also clear the list of revoked locks here?
}

bool Client::AbortTransaction() {
  if (transaction != Transaction::None) { // Nothing to do if no transaction is in progress

    // Release the locks held in all databases
    for (auto& dbEntry : openDatabases) {
      if (dbEntry.database) {
        // Release all locks held by this client and remove any queued up read operation
        dbEntry.database->AbortClientTransaction(id, dbEntry.locks);
        // FIXME STICKY only clear the locks if the client requested this in the abort!
        dbEntry.locks.clear();
      }
    }

    // Mark the transaction as gone
    transaction = Transaction::None;
    return true;
  }

  return false;
}

bool Client::IsInsideTransaction() const {
  return transaction != Transaction::None;
}


bool Client::AcquireLocks(const network::message::BlobsRead& message) {
  // The caller should ensure this is not called with an invalid database id
  assert(openDatabases.size() > message.databaseId && openDatabases[message.databaseId].database != nullptr);
  assert(IsInsideTransaction()); // Cannot acquire locks if no transaction is in progress (the caller must ensure that)
  auto& openDb = openDatabases[message.databaseId]; 

  if (openDb.database->AcquireLocks(message)) {
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


void Client::RevokeStickyLock(database_id database, const BlobLocation& lockLocation) {
  auto& dbState = openDatabases[database];
  // We only remember that we revoked the lock here we don't filter out the lock location form `locks` as this is more
  // effort to perform it on each revocation instead only once in ConstructTransactionBeginResponse()
  dbState.revokedLocks.push_back(lockLocation);
}



network::MessagePointer_T<network::message::TransactionBeginResponse> Client::ConstructTransactionBeginResponse() {
  //FIXME STICKY we should first count all the locks we want to transmit and THEN allocate a message with enough space
  auto message = network::message::TransactionBeginResponse::Create(GetOpenDatabaseCount());
  auto writePos = message->begin();
  
  database_id dbId = 0;
  for (auto& dbEntry : openDatabases) {
    if (auto db = dbEntry.database) {
      auto& entry = *writePos;
      ++writePos;

      //FIXME STICKY write the actual locks to keep/release into this header
      entry.databaseId = dbId;
      entry.keep = true;
      entry.nLocks = 0;
      //FIXME STICKY after releasing the locks also clear the list of revoked locks (we just transfered this information)
    }
    ++dbId;
  }

  return message;
}


}}