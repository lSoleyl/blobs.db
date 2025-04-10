#pragma once

#include "..\MessagePointer.hpp"
#include <string_view>
#include <vector>

namespace blobs {
namespace network {
namespace message {



struct OpenDBResponse : public Message {
  enum class Result : uint8_t {
    SUCCESS,
    DATABASE_NOT_FOUND,
    TOO_MANY_DATABASES_OPEN
  };


  Result result;  // != 0 if opening the database failed for some reason
  uint8_t databaseId; // the id to use to refer to this database in future messages


  /** Allocate a DBResponse message and return it as message pointer
   */
  static MessagePointer Create(Result result, uint8_t dbId);


  static constexpr Type type = Type::OpenDBResponse;
private:
  OpenDBResponse(Result result, uint8_t dbId); // Do not use the constructor -> use EncodeMessage
};




}
}
}