#pragma once

#include "..\MessagePointer.hpp"
#include <string_view>
#include <vector>

namespace blobs {
namespace network {
namespace message {



struct DatabaseOpenResponse : public Message {
  enum class Result : uint8_t {
    SUCCESS,
    DATABASE_OPEN_FAILED,
    ILLEGAL_DATABASE_PATH,  // Specified path is outside of the database root directory
    DATABASE_ALREADY_OPEN,
    TOO_MANY_DATABASES_OPEN,
    DATABASE_DOES_NOT_EXIST, // When opening with OpenMode::OpenFailIfNotExist and the database does not exist
    DATABASE_ALREADY_EXISTS, // When opening with OpenMode::CreateFailIfExist and the database already exists
    CANNOT_OVERWRITE_OPEN_DATABASE, // When opening with OpenMode::CreateAlways
  };


  Result result;  // != 0 if opening the database failed for some reason
  database_id databaseId; // the id to use to refer to this database in future messages


  /** Allocate a DBResponse message and return it as message pointer
   */
  static MessagePointer Create(Result result, database_id dbId);


  static constexpr Type type = Type::DatabaseOpenResponse;
private:
  DatabaseOpenResponse(Result result, database_id dbId); // Do not use the constructor -> use EncodeMessage
};


std::ostream& operator<<(std::ostream& out, const DatabaseOpenResponse& message);

}}}
