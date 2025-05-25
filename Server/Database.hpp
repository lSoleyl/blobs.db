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
   *  IMPORTANT: This only sets the locks in the database... For proper bookeeping the client also needs to know, which locks it holds
   *             Therefore locks should always be acquired via Client::AcquireLocks(), which calls this method.
   */
  bool AcquireLocks(const network::message::BlobsRead& message);


  /** Returns true if the specified client owns a write lock in this database for the specified location.
   */
  bool ClientOwnsWriteLock(client_id client, const BlobLocation& location) const;


  /** Queues a read operation for this database to be completed as soon as the conflicting locks are released.
   *  This method will also check whether this read would cause a deadlock with any already held locks and queued reads
   *  and not queue the message and return false in that case so the server can notify the client about the deadlock.
   */
  bool QueueReadCheckDeadlock(network::MessagePointer_T<network::message::BlobsRead>&& message);


  /** This method will release the specified client locks and then remove all queued up read requests for this client from the queued reads
   */
  void AbortClientTransaction(client_id client, const std::vector<BlobLocation>& locksToRelease);


  TODO("Should the database keep an open count to efficiently perform the check whether any client still uses it?")

  /** This list holds the queued reads to this database, which couldn't be immediately fulfilled due to
   *  other clients holding conflicting locks. These messages should be retried as soon as the conflicting reads are released.
   */
  std::list<network::MessagePointer_T<network::message::BlobsRead>> queuedReads;

private:
  Database(std::string name);

  /** The database snapshot representing the current transaction state, which will be replaced by a new state on each transaction commit.
   *  The transaction is committed by simply replacing the snapshot pointer with another snapshot pointer.
   * 
   *  All members are public as this class is only accessible from within the database class
   */
  class Snapshot {
  public:
    Snapshot(commit_id commitId = 1);

    /** Creates a copy of the snapshot with an incremented commit id
     */
    Snapshot(const Snapshot& other);



    /** Returns the blob at the specified location or nullptr if it doesn't exist.
     */
    Blob* GetBlob(const BlobLocation& location);


    /** Returns the segment with the specified segment id or nullptr if the segment doesn't exist.
     *  Beware: This method cannot handle `NextFreeSegmentId`!
     */
    Segment* GetSegment(segment_id segment);


    /** Global commit id counter of the last commited transaction for this database (snapshot).
     *  Initialized to 1 for a new database and is incremented with each transaction commit (and thus with each snapshot)
     *  This field is const as it is never modified, but the snapshot is instead replaced by another snapshot
     */
    const commit_id commitId;

    segment_id nextFreeSegmentId;
    std::unordered_map<segment_id, std::shared_ptr<Segment>> segments;

    Blob nextFreeSegmentIdBlob;
  };




  /** Returns true if the client can acquire a lock at the specified location with the specified access.
   *  This will NOT acquire the lock itself.
   */
  bool CanClientAcquireLock(client_id client, const BlobLocation& location, bool write);

  /** Sets a single lock at the specified location for the client and potentially upgrades a read lock into a write lock.
   *  The caller must ensure that there is no conflicting lock before calling this method (CanClientAcquireLock()) as this is not
   *  check another time.
   */
  void AcquireClientLock(client_id client, const BlobLocation& location, bool write);

  /** Releases the specified locks for the given client
   */
  void ReleaseLocks(client_id client, const std::vector<BlobLocation>& locks);

  std::string name;

  /** The database snapshot that is currently valid and updated each transaction
   */
  std::unique_ptr<Snapshot> snapshot;

  /** All currently active locks in this database
   */
  std::set<Lock, std::less<>> locks;

  static std::map<std::string, Database, std::less<>> databases;
};


}}

