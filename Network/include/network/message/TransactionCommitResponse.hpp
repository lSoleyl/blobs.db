#pragma once

#include "..\MessagePointer.hpp"
#include "..\..\common\BlobLocation.hpp"

namespace blobs {
namespace network {
namespace message {


/** Send by the server as response to the final TransactionCommit message to signal whether the commit succeeded or failed.
 *  In both cases the transaction ends and the client needs to reacquire all locks.
 *  In case the transaction ends successfully the transaction's id is returned, which can be used to update all written blobs
 *  in the client's cache to avoid reading them for the next transaction in case they didn't change.
 */
struct TransactionCommitResponse : public Message {
  enum class Result : uint8_t {
    SUCCESS,
    DATABASE_NOT_OPENED,          // Passed a database id, which the server doesn't recognize for this client
    DATABASE_ORDER_VIOLATED,      // Commit messages for multiple databases must be groupd by databses, not mixed
    MISSING_WRITE_LOCK,           // A blob has been committed without acquiring a write lock for it
    SEGMENT_DOES_NOT_EXIST,       // A segment specified in a blob commit does not exist
    CLUSTER_DOES_NOT_EXIST,       // A cluster specified in a blob commit does not exist
    BLOB_DOES_NOT_EXIST,          // A blob specified in a blob commit does not exist
    ILLEGAL_NEXT_FREE_BLOB_ID,    // An illegal value has been written into a nextFreeBlobId blob
    ILLEGAL_NEXT_FREE_CLUSTER_ID, // An illegal value has been written into a nextFreeClusterId blob
    ILLEGAL_NEXT_FREE_SEGMENT_ID, // An illegal value has been written into a nextFreeSegmentId blob
  };

  Result result;

  /** The commit id, which the server assigned to this transaction. All blobs written in this transaction will have this commit id 
   *  assigned for this modification. This commit id may thus be used by the client to update the written blob's cache ids.
   *  This is only valid if result == SUCCESS
   */
  commit_id commitId;

  /** Constructs a successful response
   */
  static MessagePointer Create(commit_id commitId);

  /** Constructs an error response
   */
  static MessagePointer CreateError(Result result);



  static constexpr Type type = Type::TransactionCommitResponse;
private:
  TransactionCommitResponse(Result result, commit_id commitId);
};

std::ostream& operator<<(std::ostream& out, const TransactionCommitResponse& message);

}}}
