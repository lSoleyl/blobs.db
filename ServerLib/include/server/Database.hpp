#pragma once

#include <common/BlobLocation.hpp>
#include <network/message/BlobsRead.hpp>
#include <network/message/TransactionCommit.hpp>
#include <network/message/DatabaseOpenResponse.hpp>

#include "Segment.hpp"
#include "Lock.hpp"
#include "File.hpp"

#include "win_include.hpp"


namespace blobs::server {

class Database {
  class Snapshot;
  class FreeList;
public:
  /** Fetch an already opened database or return nullptr
   *  The returned database may still be in a loading state.
   */
  static Database* Get(std::string_view databaseName);

  /** Opens the the database for the specified client, which will always end with a call to Server::HandleDatabaseOpenResult() for that
   *  client once the database is fully loaded (which may happen immediately).
   */
  static Database& Open(std::string_view databaseName, client_id clientId);


  /** Should be called by clients if they don't use this database anymore to delete it once the last client stopped using it.
   */
  void Release();


  /** Close this database as soon as possible
   */
  void Close();

  /** Returns a blob from this database (if it exists)
   */
  Blob* GetBlob(const BlobLocation& location);

  /** Returns a segment from this databse (if it exists)
   */
  Segment* GetSegment(segment_id segment);

  /** Returns the next free segment id, which can also be retrieved form the blob (`NextFreeSegmentId`, `NextFreeClusterId`, `NextFreeBlobId`)
   */
  segment_id GetNextFreeSegmentId() const;

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


  class CommitResult {
  public:
    CommitResult(Database& database, std::unique_ptr<Snapshot> snapshot, std::unique_ptr<FreeList> freeList);

    /** Applies the snapshot to the database and returns the commit id of that snapshot
     */
    commit_id ApplyToDatabase();

  private:
    Database& database;
    std::unique_ptr<Snapshot> snapshot;
    std::unique_ptr<FreeList> freeList;
  };


  /** Applies a range of transaction commit messages to this database and returns the new resulting snapshot.
   *  This snapshot can then be passed to SetSnapshot() commit the changest to the transient database state.
   */
  CommitResult CalculateCommitResult(network::MessagePointer_T<network::message::TransactionCommit>* commitBegin, network::MessagePointer_T<network::message::TransactionCommit>* commitEnd);


  TODO("Should the database keep an open count to efficiently perform the check whether any client still uses it?")

  /** This list holds the queued reads to this database, which couldn't be immediately fulfilled due to
   *  other clients holding conflicting locks. These messages should be retried as soon as the conflicting reads are released.
   */
  std::list<network::MessagePointer_T<network::message::BlobsRead>> queuedReads;



  //FIXME: I am not quite happy with having this class publicly available... It should be private to the database, but the database needs to return a snapshot
  //       smart pointer and the caller needs to be able to hold it...

  

private:
  Database(std::string name);


  /** Initializes the database to an in memory database structure with one segment, one cluster and one blob
   */
  void InitializeInMemory();

  /** Called to load the database structure form file. 
   *  Loading is performed in a separate thread to be able to use synchronous IO calles without blocking the server's main thread.
   *  An IOCompletionHandler will be posted to the server's IOCompletionPort upon completion to notify all waiting clients about the completed database load.
   */
  void LoadFromFile();

  /** Used internally called by LoadFromFile to write the initial file structure of the database containing a single blob
   *  This should be called after the file has been created, but not yet initialized.
   * 
   * @throws std::exception writing the initial database structure fails
   */
  void InitializeDatabaseFile();

  /** Used inside LoadFromFile to load and validate the database header and then load the snapshot and the segment list, without loading the segments yet.
   * 
   * @throws std::exception if the database is in an invalid state
   */
  void ReadInitialFileDatabaseData();


  /** Run in the server's IO completion handler once loading succeeded or failed to mark the database as loaded and inform all waiting clients about the status.
   */
  void CompleteDatabaseOpen(network::message::DatabaseOpenResponse::Result completionCode);

  /** The database snapshot representing the current transaction state, which will be replaced by a new state on each transaction commit.
   *  The transaction is committed by simply replacing the snapshot pointer with another snapshot pointer.
   */
  class Snapshot : public MemoryBlock {
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

    /** This method is internally by ApplyCommitMessage to fetch a segment for update in this transaction, which will
     *  return either a copy of the segment (if it already exists, but hasn't been copied in this transaction yet) or
     *  a new segment if it doesn't exist yet. Or the already copied segment.
     */
    Segment* UpdateSegment(segment_id segment);

    /** Deletes the segment from this snapshot by removing it from the segment map. If this was the last reference to that
     *  segment then it will also be actually 'delete'd
     */
    void DeleteSegment(segment_id segment);

    /** This method will apply the changes specified in the commit message to this snapshot.
     *  Modified Segments/Clusters/Blobs will be copied if their commitId is smaller than this snaphot's commitId
     * 
     *  The commitMessage is taken by non-const reference even though it isn't modified, because TransactionCommit at the moment
     *  has no const iterator.
     */
    void ApplyCommitMessage(network::message::TransactionCommit& commitMessage);

    /** Returns the next free segment id for this database
     *  This is the value, which can be read from the (`NextFreeSegmentId`, `NextFreeClusterId`, `NextFreeBlobId`) blob
     */
    segment_id GetNextFreeSegmentId() const;


    /** Updates the nextFreeSegmentId field and the blob's content
     */
    void SetNextFreeSegmentId(segment_id nextFreeId);

    /** Global commit id counter of the last commited transaction for this database (snapshot).
     *  Initialized to 1 for a new database and is incremented with each transaction commit (and thus with each snapshot)
     *  This field is const as it is never modified, but the snapshot is instead replaced by another snapshot
     */
    const commit_id commitId;

  private:
    segment_id nextFreeSegmentId;
    std::unordered_map<segment_id, std::shared_ptr<Segment>> segments;

    Blob nextFreeSegmentIdBlob;
  };


  /** The transient free list data structure, which is used to manage space in the database file
   */
  class FreeList : public MemoryBlock {
  public:
    FreeList(uint64_t endOffset);

    /** Enters the specified block into the free list marking it for reuse 
     */
    void FreeMemoryBlock(file::BlockReference location);

    /** A cleanup operation sorting all entries by their offset and merging adjacent blocks into a single block
     *  Should be performed BEFORE writing the free list into the database file.
     */
    void ReorganizeFreeList();



  private:
    /** End file offset(end of the memory region managed by this FreeList - everthing following that offset can be considered garbage)
     *  This field is part of the FreeList for the specific use case where the file has already been grown to allocate a new block, but the server crashes
     *  before the new FreeList is written into the Database::Root. If we load the grown database with this FreeList as still being active, we know that
     *  the memory following the endOffset is garbage and can be reused for new blocks, which we otherwise would have to assume being in use and would be lost forever.
     */
    uint64_t endOffset;

    /** Lets try with a simple vector first. Unless the free list becomes gigantic searching the whole vector should still be 
     *  a very efficient and cache locality wise optimal operation
     */
    std::vector<file::BlockReference> freeList;
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

  /** The currently active free list for this database. Only set for file databases (otherwise nullptr)
   */
  std::unique_ptr<FreeList> freeList;

  /** All currently active locks in this database
   */
  std::set<Lock, std::less<>> locks;

  bool fileDatabase; // true if the database is a file database (in contrast to a pure in-memory database)
  bool loaded;       // true if the the FILE database has been fully loaded and is ready to be used by clients
  std::vector<client_id> clientsWaitingForLoading; // Clients to notify once the database has completed loading to complete their OpenDatabse requests
  HANDLE fileHandle;
  int useCount;      // How many clients are currently using this database - the Database is closed once this count reaches 0

  static std::map<std::string, Database, std::less<>> databases;
};


}

