#include "pch.hpp"
#include "Client.hpp"


namespace blobs {
namespace server {

std::unordered_map<client_id, Client> Client::clients;

Client::Client(client_id id) : id(id) {}


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
  auto pos = std::find(openDatabases.begin(), openDatabases.end(), nullptr);
  if (pos != openDatabases.end()) {
    // simply reuse the free slot
    *pos = &db;
    return static_cast<database_id>(std::distance(openDatabases.begin(), pos));
  }

  // we must grow the databases list (unless we reached the limit)
  if (openDatabases.size() == std::numeric_limits<database_id>::max() - 1) {
    // If we were to grow it now, the index would be MAX+1, which is not a valid db index anymore
    throw std::exception("Too many databases open, cannot open another database");
  }

  openDatabases.push_back(&db);
  return static_cast<database_id>(openDatabases.size() - 1);
}


Database* Client::GetDatabase(database_id id) {
  return (id < openDatabases.size()) ? openDatabases[id] : nullptr;
}



}}