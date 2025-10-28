#pragma once

#include "..\MessagePointer.hpp"
#include "..\..\common\BlobLocation.hpp"

namespace blobs::network::message {


/** Send by the server as response to the final TransactionCommit message to signal whether the commit succeeded or failed.
 *  In both cases the transaction ends and the client needs to reacquire all locks.
 *  In case the transaction ends successfully the transaction's id is returned, which can be used to update all written blobs
 *  in the client's cache to avoid reading them for the next transaction in case they didn't change.
 * 
 *  The message contains the result and a list of locks, which the client should release/keep
 */
struct TransactionBeginResponse : public Message {
  enum class Result : uint8_t {
    SUCCESS_KEEP_LOCKS,           // Success: The message contains the locks the client may keep from its previous transaction
    SUCCESS_RELEASE_LOCKS,        // Success: The message contains the locks which have been revoked since the client's previous transaction

    ERROR_ALREADY_IN_TRANSACTION  // Already inside a transaction
  };

  Result result;

  BlobLocation* begin();
  BlobLocation* end();

  const BlobLocation* begin() const;
  const BlobLocation* end() const;

  /** True if the message holds any of the two success status codes
   */
  bool IsSuccess() const;

  /** Constructs a successful response
   *
   * @param keepLocks if true the result will be set to SUCCESS_KEEP_LOCKS, otherwise to SUCCESS_RELEASE_LOCKS
   * @param nLocks how many locks to keep/release (number of BlobLocations in this message)
   */
  static MessagePointer_T<TransactionBeginResponse> Create(bool keepLocks = true, uint16_t nLocks = 0);

  /** Constructs an error response
   */
  static MessagePointer CreateError(Result result);



  static constexpr Type type = Type::TransactionBeginResponse;
private:
  TransactionBeginResponse(Result result, message_size messageSize);
};

std::ostream& operator<<(std::ostream& out, const TransactionBeginResponse& message);

}