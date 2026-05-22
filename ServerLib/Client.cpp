#include "pch.hpp"
#include "include/server/Client.hpp"
#include "include/server/LockUtil.hpp"


namespace cpp20 {

/** The C++20 erase_if() function (without the return value)
 */
template<typename Container, typename Predicate>
void erase_if(Container& container, Predicate&& predicate) {
  container.erase(std::remove_if(container.begin(), container.end(), std::forward<Predicate>(predicate)), container.end());
}

}


namespace blobs::server {

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
  // The database use count is not modified here, this happens in Database::Open()

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
  assert(transaction == Transaction::None); // We always first have to close a running transaction and only THEN we can close the database

  if (auto database = GetDatabase(id)) {
    // Database is actually opened
    auto& dbEntry = openDatabases[id];

    // We must release all still held sticky locks (in case there are any)
    dbEntry.ApplyRevokedLocks();
    dbEntry.revokedLocks.clear();
    
    // We abuse the AbortClientTransaction() method for releasing the locks here because we don't expose a pubic
    // ReleaseLocks() method and AbortClientTransaction() doesn't perform any check for an active transaction.
    database->AbortClientTransaction(this->id, dbEntry.locks);
    dbEntry.locks.clear();

    dbEntry.database = nullptr;


    // Decrement database use count to cleanup the transiently held data structures if the database isn't needed anymore
    database->Release();

    return true; // database actually closed
  }

  return false;
}


void Client::CloseAllDatabases() {
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

bool Client::AbortTransaction(bool relaseAllLocks) {
  if (transaction != Transaction::None) { // Nothing to do if no transaction is in progress

    // Release the locks held in all databases
    for (auto& dbEntry : openDatabases) {
      if (dbEntry.database) {
        // Release all locks held by this client (if requested) and remove any queued up read operation
        
        assert(dbEntry.revokedLocks.empty()); // No lock can be revoked for a client INSIDE a transaction so this should be empty
        if (relaseAllLocks) {
          // Client wants to release all locks in transaction abort
          dbEntry.database->AbortClientTransaction(id, dbEntry.locks);
          dbEntry.locks.clear();
        } else {
          // Abort transaction, but don't release any locks -> keep them around as sticky locks for the next transaction
          dbEntry.database->AbortClientTransaction(id, {});
        }
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
    ForEachLockInMessage(*openDb.database, message, [&](const BlobLocation& location) { openDb.AddLock(location); });
    return true;
  }
   
  return false;
}


void Client::AcquireImplicitWriteLocks(database_id dbId, const std::vector<BlobLocation>& writeLocks) {
  auto& dbState = openDatabases[dbId];
  // Set locks in database
  dbState.database->AcquireImplicitWriteLocks(id, writeLocks);

  // Set locks in client's lock list
  for (auto& location : writeLocks) {
    dbState.locks.push_back(location);
  }
}


void Client::ReleaseDeletedLocks(database_id dbId, const Deleted& deleted) {
  auto& dbState = openDatabases[dbId];

  // Remove the locks for blobs which have been deleted (and release them in the database)
  cpp20::erase_if(dbState.locks, [&](const BlobLocation& lockLocation) {
    if (deleted.IsDeleted(lockLocation)) {
      // We must release the lock
      dbState.database->ReleaseLock(id, lockLocation);
      return true; // delete this lock
    }

    return false; // keep this lock
  });
}



bool Client::CommitInProcess() const {
  return !commitMessages.empty();
}


void Client::ReleaseAllLocksForDatabase(database_id database) {
  auto& dbEntry = openDatabases[database];

  // First remove all revoked locks from the list of locks (to not release a lock twice and trigger an assertion)
  dbEntry.ApplyRevokedLocks();
  dbEntry.revokedLocks.clear();

  // Then release all remaining locks
  dbEntry.database->ReleaseLocks(id, dbEntry.locks);
  dbEntry.locks.clear();
}


void Client::RevokeStickyLock(database_id database, const BlobLocation& lockLocation) {
  auto& dbState = openDatabases[database];
  // We only remember that we revoked the lock here we don't filter out the lock location form `locks` as this is more
  // effort to perform it on each revocation instead only once in ConstructTransactionBeginResponse()
  dbState.revokedLocks.push_back(lockLocation);
}


void Client::DatabaseLocks::AddLock(const BlobLocation& lockLocation) {
  auto pos = std::find(locks.begin(), locks.end(), lockLocation);
  if (pos == locks.end()) {
    // Lock not yet known
    locks.push_back(lockLocation);
  }
}

void Client::DatabaseLocks::ApplyRevokedLocks() {
  size_t keepLocks = locks.size() - revokedLocks.size();
  std::unordered_set<BlobLocation> revokeSet(revokedLocks.begin(), revokedLocks.end());
  locks.erase(std::remove_if(locks.begin(), locks.end(), [&](const BlobLocation& lock) { return revokeSet.count(lock) != 0; }), locks.end());
  assert(locks.size() == keepLocks); // Otherwise the revoke list contained duplicate entries or entries, which the client held no lock for!
  // DO NOT clear revokedLocks here (see logic in ConstructTransactionBeginResponse() on why not)
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

      // Remove all locks, which are marked for revoke from the list of locks
      dbEntry.ApplyRevokedLocks();
      

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


}