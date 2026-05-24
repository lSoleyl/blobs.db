#pragma once

#include "..\MessagePointer.hpp"
#include <string_view>

namespace blobs {
namespace network {
namespace message {



struct DatabaseOpen : public Message {
  /** This enumeration controls the requested creation/open behavior
   */
  enum class OpenMode : uint8_t {
    /** Open database and create the database if it doesn't exist yet (default)
     */
    CreateIfNotExist,

    /** Try to open the database and throw an exception if the database does not exist yet
     */
    OpenFailIfNotExist,

    /** Create a new database. Throws an exception if the database already exists
     */
    CreateFailIfExist,

    /** Always creates a new database. An existing database is overwritten with an empty database (WARNING: potential data loss!).
     *  This operation will fail if this database is currently opened by any other client.
     */
    CreateAlways
  };

  OpenMode openMode;

  // This flag is needed when opening a database during a transaction, because the transaction mode for a database
  // is usually transmitted in the TransactionBegin message.
  bool mvcc;

  // Maybe we will add some additional data here like open flags and other things... 
  // Everything following these flags will simply be the database name. We don't have to encode its length
  // because we can simply calculate it from the message length
  std::string_view GetDatabaseName() const;

  /** Encode the DatabaseOpen message and return a pointer to the message's memory
   */
  static MessagePointer Create(std::string_view databaseName, OpenMode openMode, bool mvcc);

  static constexpr Type type = Type::DatabaseOpen;
private:
  DatabaseOpen(message_size messageSize, std::string_view databaseName, OpenMode openMode, bool mvcc); // Do not use the constructor -> use Create()
};

std::ostream& operator<<(std::ostream& out, const DatabaseOpen& message);

std::ostream& operator<<(std::ostream& out, DatabaseOpen::OpenMode openMode);


}}}