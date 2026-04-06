
#include <network/message/DatabaseOpen.hpp>
#include <cassert>


namespace blobs {
namespace network {
namespace message {


DatabaseOpen::DatabaseOpen(message_size messageSize, std::string_view databaseName, OpenMode openMode) : Message(messageSize, DatabaseOpen::type), openMode(openMode) {
  std::copy_n(databaseName.data(), databaseName.size(), reinterpret_cast<char*>(this) + sizeof(DatabaseOpen));
}

std::string_view DatabaseOpen::GetDatabaseName() const {
  // Simply read the data following the message header
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(DatabaseOpen), size - sizeof(DatabaseOpen));
}


MessagePointer DatabaseOpen::Create(std::string_view databaseName, OpenMode openMode) {
  auto messageSize = static_cast<message_size>(sizeof(DatabaseOpen) + databaseName.size());
  return MessagePointer(new (new char[messageSize]) DatabaseOpen(messageSize, databaseName, openMode));
}


std::ostream& operator<<(std::ostream& out, const DatabaseOpen& message) {
  return out << message.type << '(' << message.GetDatabaseName() << ", " << message.openMode << ')';
}


std::ostream& operator<<(std::ostream& out, DatabaseOpen::OpenMode openMode) {
  switch (openMode) {
    case DatabaseOpen::OpenMode::CreateIfNotExist: return out << "CreateIfNotExist";
    case DatabaseOpen::OpenMode::OpenFailIfNotExist: return out << "OpenFailIfNotExist";
    case DatabaseOpen::OpenMode::CreateFailIfExist: return out << "CreateFailIfExist";
    case DatabaseOpen::OpenMode::CreateAlways: return out << "CreateAlways";
  }

  assert(false); // unhandled type
  return out;
}

}}}
