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

  switch (message.result) {
    case DatabaseOpenResponse::Result::SUCCESS:
      out << "dbId=" << message.databaseId;
      break;

    case DatabaseOpenResponse::Result::DATABASE_OPEN_FAILED:
      out << "DATABASE_OPEN_FAILED";
      break;

    case DatabaseOpenResponse::Result::ILLEGAL_DATABASE_PATH:
      out << "ILLEGAL_DATABASE_PATH";
      break;

    case DatabaseOpenResponse::Result::DATABASE_ALREADY_OPEN:
      out << "DATABASE_ALREADY_OPEN";
      break;

    case DatabaseOpenResponse::Result::TOO_MANY_DATABASES_OPEN:
      out << "TOO_MANY_DATABASES_OPEN";
      break;

    case DatabaseOpenResponse::Result::DATABASE_DOES_NOT_EXIST:
      out << "DATABASE_DOES_NOT_EXIST";
      break;

    case DatabaseOpenResponse::Result::DATABASE_ALREADY_EXISTS:
      out << "DATABASE_ALREADY_EXISTS";
      break;

    case DatabaseOpenResponse::Result::CANNOT_OVERWRITE_OPEN_DATABASE:
      out << "CANNOT_OVERWRITE_OPEN_DATABASE";
      break;

    default:
      assert(false); // missing stringification?
  }

  return out << ')';
}



}}}