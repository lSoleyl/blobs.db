#pragma once

#include "Database.hpp"
#include <network/message/TransactionCommit.hpp>
#include <network/message/TransactionBeginResponse.hpp>

namespace blobs::server {


/** This class represents a connected client
 */
class Client {
public:
  enum class Transaction {
    None, 
    Write,  // Regular write transaction
    MVCC,   // Not implemented yet 
  };


  static void ClientConnected(client_id id);
  
  static Client& Get(client_id id);

  /** Returns true if the given database is already opened by this client. This check is used to prevent duplicate opens
   *  of a database by a client.
   */
  bool HasDatabaseOpened(Database& db) const;

  /** Marks the specified database as opened by this client and returns the client local database id for it.
   *  Throws an exception if the client already opened 256 databases.
   */
  database_id OpenDatabase(Database& db);


  /** Closes the database opened by the client (must not be called if the client is running a transaction) and 
   *  returns true if the database was in fact opened, false otherwise.
   */
  bool CloseDatabase(database_id id);


  /** Called by the server when the client closes its connection to clean up any referenced databases and held locks.
   */
  void CloseAllDatabases();

  /** Returns the open database or nullptr if the specified id doesn't correspond to an open database
   */
  Database* GetDatabase(database_id id) const;


  /** Returns the databse id of the passed database for this client iff it has been opened by this client.
   *  Otherwise returns nullopt
   */
  std::optional<database_id> LookupDatabase(const Database& database) const;


  /** Returns the number of currently opened database by this client. The return type is chosen to be larger than
   *  database_id to also be able to encode 256 opened databases.
   */
  uint32_t GetOpenDatabaseCount() const;

  /** Returns the highest ever used database id of this client.
   *  Returns 0 if no database has ever been opened.
   */
  database_id GetMaxDatabaseId() const;

  /** Marks the client as being inside a transaction
   */
  void BeginTransaction();

  /** Aborts the client's current transaction and releases all held locks
   *  
   * @return true if there was actually a transaction running for that client
   */
  bool AbortTransaction(bool releaseAllLocks);

  /** Return true if the client has an active transaction
   */
  bool IsInsideTransaction() const;

  /** Tries to acquire all locks specified in the message in the database specified in the message.
   *  If any of the locks cannot be acquired, no lock will be acquired and the method returns false.
   * 
   * @pre message.databaseId must be a valid database index
   */
  bool AcquireLocks(const network::message::BlobsRead& message);

  /** This method is called during transaction commit to save all implicitly acquired locks (by creating the blobs)
   *  in the client as sticky locks for the next transaction. This will also
   */
  void AcquireImplicitWriteLocks(database_id dbId, const std::vector<BlobLocation>& writeLocks);

  /** Releases all locks, for all blobs, cluster, segments that have been deleted in the current transaction
   */
  void ReleaseDeletedLocks(database_id dbId, const Deleted& deleted);


  /** True if the client sent the first commit message, but not yet the final commit message.
   */
  bool CommitInProcess() const;

  /** Releases all still held locks in all databases (only used when client requests a transaction begin without keeping sticky locks)
   */
  void ReleaseAllLocks();


  /** This method is called from inside Lock::AcquireLock() on clients, which are currently not inside a transaction, but hold sticky locks, which
   *  conflict with a locking request of a currently active client. The lock is marked as revoked and will be transmitted to the client as revoked
   *  on the next transaction start.
   */
  void RevokeStickyLock(database_id database, const BlobLocation& lockLocation);

  
  /** This method is used by the server to construct the TransactionBeginResponse message for this client.
   *  This message will contain all locks to keep/release across all databases opened by this client.
   */
  network::MessagePointer_T<network::message::TransactionBeginResponse> ConstructTransactionBeginResponse();


  const client_id id;

  /** Commit messages for multi message commits are collected here, which also signifies a commit being in process and thus
   *  the server will reject any non commit message.
   */
  std::vector<network::MessagePointer_T<network::message::TransactionCommit>> commitMessages;
private:
  Client(client_id id);

  struct DatabaseLocks {
    Database* database;
    std::vector<BlobLocation> locks; // All locks held by this client in the database (vector for more memory efficient storage)
    std::vector<BlobLocation> revokedLocks; // sticky locks, which have been revoked by the server, because other clients needed access to the locked ressource


    /** Add the specified lock location into the locks vector (if not already there)
     */
    void AddLock(const BlobLocation& lockLocation);

    /** This method will remove all locks, which are in revokedLocks from the `locks` vector.
     *  IMPORTANT: for performance reasons the revokedLocks vector is NOT cleared afterwards, this is the caller's responsibility
     */
    void ApplyRevokedLocks();
  };

  /** All databases opened and locks held by this client. The index is the database id and 
   *  closed databases will be replaced with nullptr and their ids can later be reused.
   */ 
  std::vector<DatabaseLocks> openDatabases;

  /** The current transaction mode (if any opened)
   */
  Transaction transaction;

  /** A map of all connected clients
   */
  static std::unordered_map<client_id, Client> clients;
};


}