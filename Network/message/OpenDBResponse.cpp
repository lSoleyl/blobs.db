#include <network/message/OpenDBResponse.hpp>

namespace blobs {
namespace network {
namespace message {

OpenDBResponse::OpenDBResponse(Result result, uint8_t dbId) : Message(sizeof(OpenDBResponse), OpenDBResponse::type), result(result), databaseId(dbId) {}



MessagePointer OpenDBResponse::Create(Result result, uint8_t dbId) {
  return MessagePointer(new OpenDBResponse(result, dbId));
}


}}}