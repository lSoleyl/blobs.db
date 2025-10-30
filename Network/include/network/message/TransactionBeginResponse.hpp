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
    SUCCESS,                      // Success
    ERROR_ALREADY_IN_TRANSACTION  // Already inside a transaction
  };

  Result result;

  /** For EACH opened database this message contains one of the following structure specifying the database, the the mode keep/release and
   *  how many locks to keep/release. Each of this is followed by the specified number of locks
   */
  struct DatabaseStickyLocks {
    uint8_t databaseId;
    bool keep; // true = keep specified locks / false = release specified locks
    uint16_t nLocks; // number of locks to keep/release (may also be 0)

    BlobLocation* begin();
    BlobLocation* end();
  };



  class iterator {
    public:
      using value_type = DatabaseStickyLocks;
      using pointer = value_type*;
      using reference = value_type&;

      iterator(DatabaseStickyLocks* pos = nullptr);

      reference operator*() const;
      pointer operator->() const;

      iterator& operator++();

      bool operator==(const iterator& other) const;
      bool operator!=(const iterator& other) const;
    private:
      DatabaseStickyLocks* pos;
  };


  iterator begin();
  iterator end();

  /** True if the transaction has been successfully started
   */
  bool IsSuccess() const;

  /** Constructs a successful response. 
   *  After constructing it the caller must still fill the locks to keep/release for each database.
   *
   * @param nDatabases the number of databases the client has opened on this server. The message will contain
   *                   one section stating all locks, which should be released/kept PER DATABASE.
   * @param nLocks how many locks to keep/release (number of BlobLocations in this message - total for all databases)
   */
  static MessagePointer_T<TransactionBeginResponse> Create(uint32_t nDatabases, uint16_t nLocks = 0);

  /** Constructs an error response
   */
  static MessagePointer CreateError(Result result);



  static constexpr Type type = Type::TransactionBeginResponse;
private:
  TransactionBeginResponse(Result result, message_size messageSize);
};

std::ostream& operator<<(std::ostream& out, const TransactionBeginResponse& message);

}