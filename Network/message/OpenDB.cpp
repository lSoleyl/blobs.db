
#include <network/message/OpenDB.hpp>


namespace blobs {
namespace network {
namespace message {


OpenDB::OpenDB(uint32_t messageSize) : Message(messageSize, OpenDB::type) {}

std::string_view OpenDB::GetDatabaseName() const {
  // Simply read the data following the message header
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(OpenDB), size - sizeof(OpenDB));
}


MessagePointer OpenDB::Create(std::string_view databaseName) {
  uint32_t messageSize = sizeof(OpenDB) + databaseName.size();

  // new char[] should be correctly aligned to pointer size (according to the standard)
  auto memory = new char[messageSize];
  MessagePointer message(new (memory) OpenDB(messageSize));
  
  // Copy the database string
  std::copy_n(databaseName.data(), databaseName.size(), memory + sizeof(OpenDB));
  return message;
}





}}}
