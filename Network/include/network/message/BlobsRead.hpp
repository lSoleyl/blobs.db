#pragma once

#include "..\MessagePointer.hpp"
#include "..\..\common\BlobLocation.hpp"

#include <string_view>
#include <vector>

namespace blobs {
namespace network {
namespace message {

/** Read (and lock) 1-255 blobs at once
 */
struct BlobsRead : public Message {
  enum class LockMode : uint8_t {
    None = 0,   // acquire no locks (dirty read) may be performed outside of a transaction
    Read = 1,   // acquire read locks
    Write = 2,  // acquire write locks
    Delete = 3  // acquire write locks but respond with empty blob contents (the client is not interested in the blob's content)
  };

  database_id databaseId; // id referencing the previously opened database to read from
  uint8_t nBlobsRequested; // value 0 is not valid here
  LockMode lockMode; // what kind of locks should be acquired for the specified blobs?
  int32_t lockTimeoutMs; // time to wait for lock in milliseconds (<0 = wait forever, 0 = timeout immediately if there is a conflict)

  

  // One such following structure is written into the message for each requested blob
  // The requested blobs should already be sorted in ascending order to reduce the risk of deadlocks
  struct BlobAddress : public BlobLocation {
    using BlobLocation::operator=; // allow assignment from BlobLocation

    /** Only return the blob contents if the commit id of the requested blob does not match the commit id specified here.
     *  This represents the commit id of the blob that is stored in the client's cache.
     *  A database is initialized with a commit id of 1, so set 0 here to indicate that the blob is not yet in the client's cache
     *  as 0 will always be lower than even the commit id of the initial blobs of a freshly initialized database.
     *  
     *  IMPORTANT: We must also return the blob if the client cache holds a blob with a higher commit id than the database.
     *  This scenario can only happen when a client switches to an old MVCC snapshot after already reading blobs of a newer version.
     *  In that case the client MUST see the old version of the blobs to not see an inconsistent MVCC snapshot.
     */
    commit_id cacheCommitId;
  };

  /** STL like iteration over each single blob request
   */
  using iterator = BlobAddress*;
  iterator begin();
  iterator end();

  using const_iterator = const BlobAddress*;
  const_iterator begin() const;
  const_iterator end() const;


  /** Returns true if this blob read message sets a write or delete lock.
   */
  bool NeedsWriteLock() const;

  /** Returns true if the blob read message is a dirty read (i.e. lockMode == None)
   */
  bool IsDirtyRead() const;

  /** Create a new OpenDB message with sufficient space to hold nBlobsRequested number of blobs.
   *  The BlobAddresses are at this point not initialized and have to be initialized using the begin()/end() iterators
   */
  static MessagePointer_T<BlobsRead> Create(database_id databaseId, uint8_t nBlobsRequested = 1, LockMode lockMode = LockMode::Read, int32_t lockTimeoutMs = -1);


  static constexpr Type type = Type::BlobsRead;
private:
  BlobsRead(database_id databaseId, uint8_t nBlobsRequested, LockMode lockMode, int32_t lockTimeoutMs); // Do not use the constructor -> use EncodeMessage
};


std::ostream& operator<<(std::ostream& out, const BlobsRead& message);


}}}