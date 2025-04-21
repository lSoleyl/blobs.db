#include <network/message/DatabaseOpenResponse.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {

DatabaseOpenResponse::DatabaseOpenResponse(Result result, database_id dbId) : Message(sizeof(DatabaseOpenResponse), DatabaseOpenResponse::type), result(result), databaseId(dbId) {}



MessagePointer DatabaseOpenResponse::Create(Result result, database_id dbId) {
  return MessagePointer(new DatabaseOpenResponse(result, dbId));
}


std::ostream& operator<<(std::ostream& out, const DatabaseOpenResponse& message) {
  out << message.type << '(';
  if (message.result == DatabaseOpenResponse::Result::SUCCESS) {
    out << "dbId=" << message.databaseId;
  } else if (message.result == DatabaseOpenResponse::Result::DATABASE_NOT_FOUND) {
    out << "DATABASE_NOT_FOUND";
  } else if (message.result == DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN) {
    out << "TOO_MANY_DATABASES_OPEN";
  } else {
    assert(false); // missing stringification?
  }

  return out << ')';
}



}}}