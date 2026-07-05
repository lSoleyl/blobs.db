#pragma once

#include <common/BlobLocation.hpp>
#include <network/message/BlobsRead.hpp>
#include <network/message/TransactionCommit.hpp>
#include <network/message/DatabaseOpen.hpp>
#include <network/message/DatabaseOpenResponse.hpp>

#include "Segment.hpp"
#include "Lock.hpp"
#include "File.hpp"
#include "MemoryBlockDelta.hpp"
#include "FileBackend.hpp"
#include "Deleted.hpp"

#include <chrono>

namespace blobs::server {

class Database : public std::enable_shared_from_this<Database> {
  class Snapshot;
  class FreeList;
public:
  using OpenMode = network::message::DatabaseOpen::OpenMode;
  using OpenResult = network::message::DatabaseOpenResponse::Result;

  /** Fetch an already opened database or return nullptr
   *  The returned database may still be in a loading state.
   */
  static Database* Get(std::string_view databaseName);


  /** The object returned by Database::Open(). It holds the database and the error/success code
   */
  struct OpenResultStruct {
    OpenResultStruct(Database* db); // success
    OpenResultStruct(OpenResult result); // failure;

    Database* db;
    OpenResult result;

    explicit operator bool() const;
  };

  /** Opens the the database for the specified client and returns the fully loaded database or an error status in response.
   *  The returned database's use count will have already been incremented in case of success.
   * 
   * @param databaseName the database name as specified in the message (may start with "mem:" to refer to an in memory database)
   * @param openMode how the client requested to open/create the database
   */
  static OpenResultStruct Open(std::string_view databaseName, OpenMode openMode);


  /** Should be called by clients if they don't use this database anymore to delete it once the last client stopped using it.
   */
  void Release();

  /** This is called by clients on transaction begin for all databases that should be opened in MVCC mode to set the current snapshot 
   *  or increment the use count on the current snapshot.
   */
  void BeginMVCC();

  /** This is called by a client after transaction commit/abort for all databases that were opened in MVCC to reduce the reference count
   *  on the MVCC snapshot and release the snapshot if it reaches zero.
   */
  void EndMVCC();


  /** Close this database as soon as possible
   */
  void Close();

  /** Returns a blob from this database (if it exists)
   *  And loads it from disk if it hasn't yet been loaded
   * 
   * @param location the blob location to return
   * @param mvcc if true then the blob is read from the MVCC snapshot
   */
  Blob* GetLoadedBlob(const BlobLocation& location, bool mvcc = false);

  /** Return the specified cluster from this database (if it exists) 
   *  and loads it from disk if not yet done.
   */
  Cluster* GetLoadedCluster(segment_id segment, cluster_id cluster, bool mvcc = false);

  /** Returns a segment from this databse (if it exists)
   *  and loads it from disk if not yet done.
   * 
   * @param segment the segment to load
   * @param mvcc if true then the segment is read from the MVCC snapshot
   */
  Segment* GetLoadedSegment(segment_id segment, bool mvcc = false);

  /** Returns the next free segment id, which can also be retrieved form the blob (`NextFreeSegmentId`, `NextFreeClusterId`, `NextFreeBlobId`)
   */
  segment_id GetNextFreeSegmentId() const;

  /** Returns the current snapshot's commit id
   */
  commit_id GetCommitId() const;

  // Iteration over all segment objects of the current snapshot/mvcc snapshot.
  using iterator = typename sorted_flat_map<segment_id, std::shared_ptr<Segment>>::iterator;
  iterator begin(bool mvcc);
  iterator end(bool mvcc);

  /** Acquires the locks specified in the blobs read message and returns true if successful, false if not
   *  IMPORTANT: This only sets the locks in the database... For proper bookkeeping the client also needs to know, which locks it holds
   *             Therefore locks should always be acquired via Client::AcquireLocks(), which calls this method.
   */
  bool AcquireLocks(const network::message::BlobsRead& message);


  /** This method is called during transaction commit to assign implicitly acquired write locks (client created the blobs)
   *  to this client to preserve these write locks across transaction end as sticky locks.
   * 
   *  IMPORTANT: This method performs NO checks whether the locks can actually be acquired due to the fact that the created blobs
   *             should not exist before and thus no locks should exist for these blobs.
   *             Also do not call this method directly, call Client::AcquireImplicitWriteLocks() instead to keep database and client state in sync
   */
  void AcquireImplicitWriteLocks(client_id client, const std::vector<BlobLocation>& writeLocks);


  /** Returns true if the specified client owns a write lock in this database for the specified location.
   */
  bool ClientOwnsWriteLock(client_id client, const BlobLocation& location) const;



  struct DeadlockInfo {
    struct Request {
      BlobLocation location;
      client_id client;
      bool writeLock;
    };

    Request requests[2];

    std::string ToString() const;
  };

  /** Queues a read operation for this database to be completed as soon as the conflicting locks are released.
   *  This method will also check whether this read would cause a deadlock with any already held locks and queued reads.
   *  The result will be a list of all clients with a detected deadlock.
   * 
   * @param message the BlobsRead message to check for deadlocks and queue into the queued read operations
   * 
   * @return a vector of deadlocks triggered by queueing this message. The returned vector is empty in case there are no conflicts.
   */
  std::vector<DeadlockInfo> QueueReadCheckDeadlock(network::MessagePointer_T<network::message::BlobsRead>&& message);


  /** This method will release the specified client locks and then remove all queued up read requests for this client from the queued reads
   */
  void AbortClientTransaction(client_id client, const std::vector<BlobLocation>& locksToRelease);

  //FIXME: I am not quite happy with having this class publicly available... It should be private to the database, but the database needs to return a snapshot
  //       smart pointer and the caller needs to be able to hold it...
  class CommitResult {
  public:
    CommitResult(Database& database, std::shared_ptr<Snapshot> snapshot, std::unique_ptr<FreeList> freeList, std::unique_ptr<MemoryBlockDelta> delta, Deleted&& deleted);

    /** Applies the snapshot to the database and returns the commit id of that snapshot
     */
    commit_id ApplyToDatabase();

    Deleted deleted;
  private:
    Database& database;
    std::shared_ptr<Snapshot> snapshot;
    std::unique_ptr<FreeList> freeList;
    std::unique_ptr<MemoryBlockDelta> delta;
  };


  /** Applies a range of transaction commit messages to this database and returns the new resulting snapshot.
   *  This snapshot can then be passed to SetSnapshot() commit the changest to the transient database state.
   */
  CommitResult CalculateCommitResult(network::MessagePointer_T<network::message::TransactionCommit>* commitBegin, network::MessagePointer_T<network::message::TransactionCommit>* commitEnd);


  struct QueuedMessage {
    network::message::BlobsRead* operator->() const { return message.Get(); }

    network::MessagePointer_T<network::message::BlobsRead> message; // the read request itself
    std::optional<std::chrono::high_resolution_clock::time_point> lockTimeoutTime; // when should the message be cancelled due to a lock timeout
  };

  /** This list holds the queued reads to this database, which couldn't be immediately fulfilled due to
   *  other clients holding conflicting locks. These messages should be retried as soon as the conflicting locks are released.
   */
  std::list<QueuedMessage> queuedReads;

  /** Releases the specified lock for the given client
   */
  void ReleaseLock(client_id client, const BlobLocation& lock);

  /** Releases the specified locks for the given client
   */
  void ReleaseLocks(client_id client, const std::vector<BlobLocation>& locks);

private:
  Database(std::string name);
  Database(const Database&) = delete; // Not copyable, not movable
  Database& operator=(const Database&) = delete;

  /** Initializes the database to an in memory database structure with one segment, one cluster and one blob
   */
  OpenResult InitializeInMemory();

  /** Called to load the database structure form file.
   *  Loading is performed synchronously in the main thread for now (opening a new file database will thus block the whole server).
   * 
   * @param openMode the opening/creation mode specified by the client
   * 
   * @return the database loading result code
   */
  OpenResult LoadFromFile(OpenMode openMode);

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
     *  This will also load the segment, cluster, blob from the database file into memory if not already done.
     * 
     * @param location the database location to retrieve the blob from (segment, cluster, blob)
     * @param file the database file to load the segmnt/cluster/blob from (only used for file databases)
     */
    Blob* GetLoadedBlob(const BlobLocation& location, const FileBackend& file);

    /** Returns the cluster at the sepcified location or nullptr if it doesn't exist
     *  This will also load the segment and cluster from the database file into memory if not already done.
     * 
     * @param segment the cluster's segment
     * @param cluster the cluster to load
     * @param file the database file to load the segment/cluster from (only used for file databases)
     */
    Cluster* GetLoadedCluster(segment_id segment, cluster_id cluster, const FileBackend& file);


    /** Returns the specified segment or nullptr if it doesn't exist
     *  This will also load the segment from the database file into memory if not already done.
     * 
     * @param segment the segment to load
     * @param file the database file to laod the segment/cluster from (only used for file databases)
     */
    Segment* GetLoadedSegment(segment_id segment, const FileBackend& file);


    /** Returns the segment with the specified segment id or nullptr if the segment doesn't exist.
     *  Beware: This method cannot handle `NextFreeSegmentId`!
     *  This method may return a segment, which is not yet loaded from disk into memory.
     */
    Segment* GetSegment(segment_id segment);

    /** This method is internally by ApplyCommitMessage to fetch a segment for update in this transaction, which will
     *  return either a copy of the segment (if it already exists, but hasn't been copied in this transaction yet) or
     *  a new segment if it doesn't exist yet. Or the already copied segment.
     * 
     * @param segment the segment id to retrieve the segment for
     * @param delta the data structure used to keep track of created/deleted memory blocks during copy-on-write commit processing
     *              This may be nullptr for pure in memory databases
     */
    Segment* UpdateSegment(segment_id segment, MemoryBlockDelta* delta);

    /** Used when loading a snapshot to mark the segment as exisitng, but not yet loaded from file
     */
    void DelayLoadSegment(segment_id segment, file::BlockReference& fileLocation);

    /** Deletes the segment from this snapshot by removing it from the segment map. If this was the last reference to that
     *  segment then it will also be actually 'delete'd
     * 
     * @param segment the segment id of the segment to delete
     * @param delta the data structure used to keep track of created/deleted memory blocks during copy-on-write commit processing
     *              This may be nullptr for pure in memory databases
     */
    void DeleteSegment(segment_id segment, MemoryBlockDelta* delta);

    /** This method will apply the changes specified in the commit message to this snapshot.
     *  Modified Segments/Clusters/Blobs will be copied if their commitId is smaller than this snaphot's commitId
     * 
     *  The commitMessage is taken by non-const reference even though it isn't modified, because TransactionCommit at the moment
     *  has no const iterator.
     * 
     *  We specifically update two free list structures here to:
     *  - FIRST allocate all the required memory for new memory blocks from the current free list and
     *  - THEN releasing all the memory of the previous memory blocks
     *  The caller can then finally merge both together into one resulting freeList.
     *  Only then can we perform a safe copy-on-write update of the database (by not creating new data in currently occupied blocks).
     * 
     * @param commitMessage the commit message to apply to this snapshot's state
     * @param delta the data structure used to keep track of allocated/released blocks during copy-on-write commits
     *              This may be nullptr for pure in memory databases
     * @param deleted this data structure is used to track all deleted blobs, clusters and segments in this database
     * @param file the database's file backend. It is only used when deleting blobs,cluster,segments while having an active mvcc snapshot
     *             to load them from file into memory before deleting them in case they will be referenced inside the snapshot.   
     * @param hasMVCCSnapshot true if this snapshot's database has an active MVCC database and we must fully load all blobs,clusters,segments that we want to delete.
     *                        This has been defined as template parameter to avoid checking it for each updated blob of the commit.
     */
    template<bool hasMVCCSnapshot>
    void ApplyCommitMessage(network::message::TransactionCommit& commitMessage, MemoryBlockDelta* delta, Deleted& deleted, FileBackend& file);

    /** Returns the next free segment id for this database
     *  This is the value, which can be read from the (`NextFreeSegmentId`, `NextFreeClusterId`, `NextFreeBlobId`) blob
     */
    segment_id GetNextFreeSegmentId() const;


    /** Updates the nextFreeSegmentId field and the blob's content
     */
    void SetNextFreeSegmentId(segment_id nextFreeId);


    /** Calculate the size of the Segment's memory block if stored in file
     */
    virtual uint64_t CalculateRequiredSize() const override;

    /** Serialize the snapshot into a buffer for writing it to file
     */
    virtual void SerializeIntoBuffer(std::vector<char>& targetBuffer) const override;

    // Iteration over all segment objects of this snapshot.
    using iterator = typename sorted_flat_map<segment_id, std::shared_ptr<Segment>>::iterator;
    iterator begin();
    iterator end();

    /** Global commit id counter of the last commited transaction for this database (snapshot).
     *  Initialized to 1 for a new database and is incremented with each transaction commit (and thus with each snapshot)
     *  This field is const as it is never modified, but the snapshot is instead replaced by another snapshot
     */
    const commit_id commitId;

  private:
    segment_id nextFreeSegmentId;
    sorted_flat_map<segment_id, std::shared_ptr<Segment>> segments;

    Blob nextFreeSegmentIdBlob;
  };


  /** The transient free list data structure, which is used to manage space in the database file
   */
  class FreeList : public MemoryBlock {
  public:
    FreeList(uint64_t endOffset);

    /** Enters the specified block into the free list marking it for reuse. 
     *  By calling this method the FreeList becomes unorganized and Reorganize() must be called 
     *  before writing the FreeList to disk.
     */
    void FreeMemoryBlock(file::BlockReference location);

    /** Allocates a memory block of the specified size in the file by selecting a matching free list.
     * By calling this method the FreeList may become unorganized and Reorganize() must be called 
     *  before writing the FreeList to disk.
     * 
     * @return the file offset where the block could be allocated
     */
    uint64_t AllocateMemoryBlock(uint64_t size);


    /** A cleanup operation sorting all entries by their offset and merging adjacent blocks into a single block
     *  Should be performed BEFORE writing the free list into the database file.
     */
    void Reorganize();

    /** Calculate the size of the FreeList's memory block if stored in file
     */
    virtual uint64_t CalculateRequiredSize() const override;

    /** Serialize the free list into a buffer for writing it to file
     */
    virtual void SerializeIntoBuffer(std::vector<char>& targetBuffer) const override;

    using iterator = std::vector<file::BlockReference>::iterator;

    /** Iteration over all free memory blocks
     */
    iterator begin();
    iterator end();

    /** Returns the number of free list entries in this list
     */
    size_t Size() const;
    

    /** This method will first mark the free list's memory block for release.
     *  This method will then allocate memory for all memory blocks marked for allocation in delta.
     *  It will then estimate the required size needed to store the modified free list in file.
     *  A block of that size will then be allocated from this free list.
     *  Then all released memory blocks will be marked as free in this free list.
     *  
     *  Finally the free list is reorganized, which may remove some entries and shrink it in size.
     *  The memory block itself is not resized to account for this.
     */
    void AllocateAndApplyDelta(MemoryBlockDelta& delta);

  private:
    /** Estimates the maximum required memory size needed to store this free list if
     *  it should be merged with the given other free list, stored as new free blocks AND
     *  the memory block for storing this free list will be allocated form the free list before
     *  releasing the specified number of blocks.
     *
     * @param additionalFreeBlocks the number of additional free blocks, which will be entered into the free list BEFORE writing it to disk
     */
    uint64_t EstimateRequiredSize(uint64_t additionalFreeBlocks) const;

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
   * 
   *  The special blob location DeleteClusterId is not handled here, it is handled by AcquireLocks(). This method will treat 
   *  DeleteClusterId like any other blob
   */
  bool CanClientAcquireLock(client_id client, const BlobLocation& location, bool write);

  /** Sets a single lock at the specified location for the client and potentially upgrades a read lock into a write lock.
   *  The caller must ensure that there is no conflicting lock before calling this method (CanClientAcquireLock()) as this is not
   *  checked another time.
   */
  void AcquireClientLock(client_id client, const BlobLocation& location, bool write);


  /** This structure is used when performing deadlock detection to collect all clients preventing 
   *  the current message's lock acquisition
   */
  struct LockConflict {
    LockConflict(const BlobLocation& location, client_id blockedBy);

    BlobLocation location;
    client_id blockedBy;
  };


  /** Collects all conflicting clients preventing lock acuisition for this message. Only used for deadlock detection.
   */
  std::vector<LockConflict> CollectLockConflicts(const network::message::BlobsRead& message);

  /** If the given message has a lock conflict with the specified client then the location of that conflict (from this message)
   *  will be returned, otherwise nullopt is returned. Only used for deadlock detection.
   */
  std::optional<BlobLocation> FindLockConflictWith(const network::message::BlobsRead& message, client_id conflictingClient);


  /** Searches all queued reads for reads with a lock timeout value <= steady_clock::now() and cancels these requests with a corresponding error message.
   */
  void CheckForLockTimeouts();


  /** Implements the check of whether certain locks can be revoked (are sticky locks)
   *  and revokation of said locks
   */
  class StickyLockHandler : public StickyLockInterface {
    public:
      StickyLockHandler(const Database& db);

      /** Should return true if the client is not currently inside a transaction and thus a sticky lock
       *  can be revoked from it
       */
      virtual bool CanRevokeStickyLock(client_id id) const override;

      /** Revokes a sticky lock from a currently inactive client
       */
      virtual void RevokeStickyLock(client_id id, const BlobLocation& location) override;

      const Database& db;
  };


  std::string name;



  /** The database snapshot that is currently valid and updated each transaction.
   *  This is a shared pointer as the current mvcc snapshot and the active snapshot may reference the same object.
   */
  std::shared_ptr<Snapshot> snapshot;

  /** The currently active free list for this database. Only set for file databases (otherwise nullptr)
   */
  std::unique_ptr<FreeList> freeList;

  /** All currently active locks in this database
   */
  std::set<Lock, std::less<>> locks;

  /** If a client uses this database in MVCC mode, then the current snapshot is copied into the mvccSnapshot for all clients using this database in MVCC.
   */
  std::shared_ptr<Snapshot> mvccSnapshot;

  bool fileDatabase; // true if the database is a file database (in contrast to a pure in-memory database)
  FileBackend file;
  file::Database fileHeader; // The current database file header (this is stored here to avoid having to read it to update the database) - the header state is undefined for in memory databases
  int useCount;      // How many clients are currently using this database - the Database is closed once this count reaches 0
  int mvccUseCount;  // How many clients are currently using this databse in MVCC (ie. how many need the current mvcc snapshot). The snapshot is discarded once the count reaches 0.

  StickyLockHandler stickyLockHandler;

  /** A marker when performing delayed database close to recognize whether the database has been opened and closed in between
   *  scheduling the close task.
   */
  std::chrono::high_resolution_clock::time_point delayedCloseAt;

  /** All opened databases on the server. The database is held in a shared_ptr because the lock timout check Task
   *  may be scheduled to run AFTER the database has already been closed and we need a safe way to check for this.
   */
  static std::vector<std::shared_ptr<Database>> databases;


  friend class DatabaseDocTestAccess;
};


}

