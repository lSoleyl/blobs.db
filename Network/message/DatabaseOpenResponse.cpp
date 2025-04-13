#include <network/message/DatabaseOpenResponse.hpp>

namespace blobs {
namespace network {
namespace message {

DatabaseOpenResponse::DatabaseOpenResponse(Result result, uint8_t dbId) : Message(sizeof(DatabaseOpenResponse), DatabaseOpenResponse::type), result(result), databaseId(dbId) {}



MessagePointer DatabaseOpenResponse::Create(Result result, uint8_t dbId) {
  return MessagePointer(new DatabaseOpenResponse(result, dbId));
}


}}}