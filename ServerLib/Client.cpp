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
    // Database is actually opened
    auto& dbEntry = openDatabases[id];

    //FIXME STICKY Release any still held sticky locks
    //FIXME STICKY No, the below assertion doesn't hold for sticky locks
    assert(openDatabases[id].locks.empty()); // since no transaction should be running
    dbEntry.database = nullptr;


    // Decrement database use count to cleanup the transiently held data structures if the database isn't needed anymore
    database->Release();

    return true; // database actually closed
  }

  return false;
}


void Client::CloseAllDatabases() {
  assert(transaction == Transaction::None); // We should not perform this while inside a transaction... right?

  database_id id = 0;
  for (auto& entry : openDatabases) {
    if (entry.database) {
      CloseDatabase(id);
    }
    ++id;
  }
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
}

bool Client::AbortTransaction() {
  if (transaction != Transaction::None) { // Nothing to do if no transaction is in progress

    // Release the locks held in all databases
    for (auto& dbEntry : openDatabases) {
      if (dbEntry.database) {
        // Release all locks held by this client and remove any queued up read operation
        
        // FIXME STICKY only clear the locks if the client requested this in the abort message!
        // FIXME STICKY if we actually clear the locks here, then we must however also consolidate&clear the revoked lock list
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
  // First count the total number of locks to keep/revoke across all databases since the last transaction

  size_t totalLocks = 0;
  uint32_t nDatabases = 0;
  for (auto& dbEntry : openDatabases) {
    if (auto db = dbEntry.database) {
      assert(dbEntry.locks.size() >= dbEntry.revokedLocks.size()); // Otherwise we revoked a lock twice or revoked a non existing lock
      totalLocks += std::min(dbEntry.locks.size() - dbEntry.revokedLocks.size(), dbEntry.revokedLocks.size());
      ++nDatabases;
    }
  }
 
  if (totalLocks > std::numeric_limits<uint16_t>::max()) {
    // We have too many locks to keep/revoke.
    //FIXME STICKY we can still handle this case by simply keeping fewer locks than would be possible.
    //             We should handle this case in a special method, which will attempt to cramp as many locks
    //             per database into the available message space.
    // 
    //FIXME STICKY A trivial way to handle this would be to simply revoke all locks in such a case, but this could cause
    //             an unnecessary performance hit... On the other hand the cramped situation may continue unless we revoke a lot
    //             of long held but unused locks.
    assert(false);
  }
  
  
  // Allocate a large enough message to hold all lock information
  auto message = network::message::TransactionBeginResponse::Create(nDatabases, static_cast<uint16_t>(totalLocks));
  auto writePos = message->begin();

  
  database_id dbId = 0;
  for (auto& dbEntry : openDatabases) {
    if (auto db = dbEntry.database) {
      auto& entry = *writePos;
      ++writePos;

      entry.databaseId = dbId;

      // Determine the locks to keep by filtering out all locks, which should be revoked
      std::unordered_set<BlobLocation> revokeSet(dbEntry.revokedLocks.begin(), dbEntry.revokedLocks.end());
      
      // Remove all locks, which are marked for revoke from the list of locks
      size_t keepLocks = dbEntry.locks.size() - dbEntry.revokedLocks.size();
      dbEntry.locks.erase(std::remove_if(dbEntry.locks.begin(), dbEntry.locks.end(), [&](const BlobLocation& lock) { return revokeSet.count(lock) != 0; }), dbEntry.locks.end());
      assert(dbEntry.locks.size() == keepLocks); // Otherwise the revoke list contained duplicate entries or entries, which the client held no lock for!

      // Now enter the smaller number of locks into the message
      entry.keep = dbEntry.locks.size() <= dbEntry.revokedLocks.size();
      auto& locks = entry.keep ? dbEntry.locks : dbEntry.revokedLocks;
      entry.nLocks = static_cast<uint16_t>(locks.size());
      
      auto writePos = entry.begin();
      for (auto& lock : locks) {
        *writePos = lock;
        ++writePos;
      }


      // After releasing the locks also clear the list of revoked sticky locks (we just transferred this information to the client)
      dbEntry.revokedLocks.clear();
    }
    ++dbId;
  }

  return message;
}


}}