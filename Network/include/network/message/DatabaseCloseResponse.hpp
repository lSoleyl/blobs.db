#pragma once

#include "..\MessagePointer.hpp"

namespace blobs {
namespace network {
namespace message {

/** This message is sent by the client when closing a database and is then sent back to the client as confirmation.
 */
struct DatabaseCloseResponse : public Message {
public:
  enum class Result : uint8_t {
    SUCCESS,
    TRANSACTION_IN_PROGRESS,
    DATABASE_NOT_OPEN
  };

  Result result;

  /** Encode the DatabaseClose message
   */
  static MessagePointer Create(Result result);

  static constexpr Type type = Type::DatabaseCloseResponse;
private:
  DatabaseCloseResponse(Result result);
};

std::ostream& operator<<(std::ostream& out, DatabaseCloseResponse::Result result);

std::ostream& operator<<(std::ostream& out, const DatabaseCloseResponse& message);


}}}
