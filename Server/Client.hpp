#pragma once

#include "Database.hpp"

namespace blobs {
namespace server {


/** This class represents a connected client
 */
class Client {
public:
  static void ClientConnected(client_id id);
  
  static Client& Get(client_id id);

  /** Marks the specified database as opened by this client and returns the client local database id for it.
   *  Throws an exception if the client already opened 256 databases.
   */
  database_id OpenDatabase(Database& db);

  /** Returns the open database or nullptr if the specified id doesn't correspond to an open database
   */
  Database* GetDatabase(database_id id);


  const client_id id;
private:
  Client(client_id id);

  TODO("we also need a list of all held locks by this client.. or should it be held by the Transaction object")
  

  /** All databases opened by this client. The index is the database id and 
   *  closed databases will be replaced with nullptr and their ids can later be reused.
   */ 
  std::vector<Database*> openDatabases;

  /** A map of all connected clients
   */
  static std::unordered_map<client_id, Client> clients;
};


}}