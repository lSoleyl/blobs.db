#include <network/message/DatabaseClose.hpp>

namespace blobs {
namespace network {
namespace message {

DatabaseClose::DatabaseClose(database_id databaseId) : Message(sizeof(DatabaseClose), DatabaseClose::type), databaseId(databaseId) {}

MessagePointer DatabaseClose::Create(database_id databaseId) {
  return MessagePointer(new DatabaseClose(databaseId));
}

}}}