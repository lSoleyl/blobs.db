#pragma once

#include <common/BlobLocation.hpp>
#include <network/message/BlobsRead.hpp>

#include "Segment.hpp"
#include "Lock.hpp"


namespace blobs {
namespace server {

class Database {
public:
  TODO(
    "Later we must separate these two paths as opening will mean reading and parsing a file, while "
    "fetching is simply a memory lookup.We shouldn't block the server while watiting for the database to be fully loaded and ready."
  )

  /** Fetch an already opened database or open the specified database 
   */
  static Database& Get(std::string_view databaseName);

  /** Returns a blob from this database (if it exists)
   */
  Blob* GetBlob(const BlobLocation& location);

  /** Returns a segment from this databse (if it exists)
   */
  Segment* GetSegment(segment_id segment);

  /** Acquires the locks specified in the blobs read message and returns true if successful, false if not
   */
  bool AcquireLocks(const network::message::BlobsRead& message);


  /** Queues a read operation for this database to be completed as soon as the conflicting locks are released.
   *  This method will also check whether this read would cause a deadlock with any already held locks and queued reads
   *  and not queue the message and return false in that case so the server can notify the client about the deadlock.
   */
  bool QueueReadCheckDeadlock(network::MessagePointer_T<network::message::BlobsRead>&& message);


  /** This method will release the specified client locks and then remove all queued up read requests for this client from the queued reads
   */
  void AbortClientTransaction(client_id client, const std::vector<BlobLocation>& locksToRelease);
  

  TODO("Should the database keep an open count to efficiently perform the check whether any client still uses it?")
private:
  Database(std::string name);

  /** Releases the specified locks for the given client
   */
  void ReleaseLocks(client_id client, const std::vector<BlobLocation>& locks);

  std::string name;

  /** Global commit id counter of the last commited transaction for this database.
   *  Initialized to 1 for a new database.
   */
  commit_id commitId;
  segment_id lastSegmentId;
  std::unordered_map<segment_id, std::unique_ptr<Segment>> segments;

  /** This list holds the queued reads to this database, which couldn't be immediately fulfilled due to 
   *  other clients holding conflicting locks. These messages should be retried as soon as the conflicting reads are released.
   */
  std::list<network::MessagePointer_T<network::message::BlobsRead>> queuedReads;

  /** All currently active locks in this database
   */
  std::set<Lock, std::less<>> locks;

  static std::map<std::string, Database, std::less<>> databases;
};



}}

