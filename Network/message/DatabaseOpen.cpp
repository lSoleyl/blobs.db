
#include <network/message/DatabaseOpen.hpp>


namespace blobs {
namespace network {
namespace message {


DatabaseOpen::DatabaseOpen(uint32_t messageSize, std::string_view databaseName) : Message(messageSize, DatabaseOpen::type) {
  std::copy_n(databaseName.data(), databaseName.size(), reinterpret_cast<char*>(this) + sizeof(DatabaseOpen));
}

std::string_view DatabaseOpen::GetDatabaseName() const {
  // Simply read the data following the message header
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(DatabaseOpen), size - sizeof(DatabaseOpen));
}


MessagePointer DatabaseOpen::Create(std::string_view databaseName) {
  auto messageSize = static_cast<uint32_t>(sizeof(DatabaseOpen) + databaseName.size());
  return MessagePointer(new (new char[messageSize]) DatabaseOpen(messageSize, databaseName));
}





}}}
