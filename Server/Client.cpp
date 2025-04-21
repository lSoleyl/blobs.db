#include "pch.hpp"
#include "Client.hpp"


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



database_id Client::OpenDatabase(Database& db) {

  // First find a nullptr to use inside 
  auto pos = std::find_if(openDatabases.begin(), openDatabases.end(), [](auto& entry) { return entry.database == nullptr; });
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


Database* Client::GetDatabase(database_id id) {
  return (id < openDatabases.size()) ? openDatabases[id].database : nullptr;
}

void Client::AbortTransaction() {
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
  }
}



}}