#include <network/message/DatabaseClose.hpp>

namespace blobs {
namespace network {
namespace message {

DatabaseClose::DatabaseClose(uint8_t databaseId) : Message(sizeof(DatabaseClose), DatabaseClose::type), databaseId(databaseId) {}

MessagePointer DatabaseClose::Create(uint8_t databaseId) {
  return MessagePointer(new DatabaseClose(databaseId));
}

}}}