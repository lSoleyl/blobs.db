#include "pch.hpp"
#include "Client.hpp"


namespace blobs {
namespace server {

std::unordered_map<uint16_t, Client> Client::clients;

Client::Client(uint16_t id) : id(id) {}


void Client::ClientConnected(uint16_t id) {
  auto inserted = clients.emplace(id, Client(id)).second;
  //TODO: we should return some kind of error code and cancel the connection if there is already a client with this id
  assert(inserted);
}


Client& Client::Get(uint16_t id) {
  auto pos = clients.find(id);
  assert(pos != clients.end());
  return pos->second;
}



uint8_t Client::OpenDatabase(Database& db) {

  // First find a nullptr to use inside 
  auto pos = std::find(openDatabases.begin(), openDatabases.end(), nullptr);
  if (pos != openDatabases.end()) {
    // simply reuse the free slot
    *pos = &db;
    return static_cast<uint8_t>(std::distance(openDatabases.begin(), pos));
  }

  // we must grow the databases list (unless we reached the limit)
  if (openDatabases.size() == std::numeric_limits<uint8_t>::max() - 1) {
    // If we were to grow it now, the index would be MAX+1, which is not a valid db index anymore
    throw std::exception("Too many databases open, cannot open another database");
  }

  openDatabases.push_back(&db);
  return static_cast<uint8_t>(openDatabases.size() - 1);
}


}}