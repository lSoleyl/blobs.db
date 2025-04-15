#include <network/message/DatabaseOpenResponse.hpp>

namespace blobs {
namespace network {
namespace message {

DatabaseOpenResponse::DatabaseOpenResponse(Result result, database_id dbId) : Message(sizeof(DatabaseOpenResponse), DatabaseOpenResponse::type), result(result), databaseId(dbId) {}



MessagePointer DatabaseOpenResponse::Create(Result result, database_id dbId) {
  return MessagePointer(new DatabaseOpenResponse(result, dbId));
}


}}}