#pragma once

#include "..\MessagePointer.hpp"
#include <string_view>
#include <vector>

namespace blobs {
namespace network {
namespace message {

/** This message is sent by the client when closing a database and is then sent back to the client as confirmation.
 */
struct DatabaseClose : public Message {
public:
  database_id databaseId; // the database to close

  /** Encode the DatabaseClose message
   */
  static MessagePointer Create(database_id databaseId);

  static constexpr Type type = Type::DatabaseClose;
private:
  DatabaseClose(database_id databaseId);
};

std::ostream& operator<<(std::ostream& out, const DatabaseClose& message);


}}}


