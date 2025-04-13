#pragma once

#include "Database.hpp"

namespace blobs {
namespace server {


/** This class represents a connected client
 */
class Client {
public:
  static void ClientConnected(uint16_t id);
  
  static Client& Get(uint16_t id);

  /** Marks the specified database as opened by this client and returns the client local database id for it.
   *  Throws an exception if the client already opened 256 databases.
   */
  uint8_t OpenDatabase(Database& db);


  const uint16_t id;
private: 
  Client(uint16_t id);

  //TODO: we also need a list of all held locks by this client
  

  /** All databases opened by this client. The index is the database id and 
   *  closed databases will be replaced with nullptr and their ids can later be reused.
   */ 
  std::vector<Database*> openDatabases;

  /** A map of all connected clients
   */
  static std::unordered_map<uint16_t, Client> clients;
};


}}