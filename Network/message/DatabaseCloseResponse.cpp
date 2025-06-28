#include <network/message/DatabaseCloseResponse.hpp>
#include <cassert>

namespace blobs::network::message {


DatabaseCloseResponse::DatabaseCloseResponse(Result result) : Message(sizeof(DatabaseCloseResponse), DatabaseCloseResponse::type), result(result) {}

MessagePointer DatabaseCloseResponse::Create(Result result) {
  return MessagePointer(new DatabaseCloseResponse(result));
}

std::ostream& operator<<(std::ostream& out, DatabaseCloseResponse::Result result) {
  switch (result) {
    case DatabaseCloseResponse::Result::SUCCESS: return out << "SUCCESS";
    case DatabaseCloseResponse::Result::TRANSACTION_IN_PROGRESS: return out << "TRANSACTION_IN_PROGRESS";
    case DatabaseCloseResponse::Result::DATABASE_NOT_OPEN: return out << "DATABASE_NOT_OPEN";
  }

  assert(false); // unhandled result type
  return out;
}

std::ostream& operator<<(std::ostream& out, const DatabaseCloseResponse& message) {
  return out << message.type << '(' << message.result << ')';
}





}