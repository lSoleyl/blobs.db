
#include <network/message/OpenDB.hpp>


namespace blobs {
namespace network {
namespace message {


std::string_view OpenDB::GetDatabaseName() const {
  // Simply read the data following the message header
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(OpenDB), size - sizeof(OpenDB));
}


std::string_view OpenDB::EncodeMessage(std::vector<char>& targetBuffer, std::string_view databaseName) {
  uint32_t messageSize = sizeof(OpenDB) + databaseName.size();

  if (targetBuffer.size() < messageSize) {
    targetBuffer.resize(messageSize);
  }

  // Set header properties
  OpenDB& message = *reinterpret_cast<OpenDB*>(targetBuffer.data());
  message.size = messageSize;
  message.type = Type::OpenDB;

  // Copy the database string
  std::copy_n(databaseName.data(), databaseName.size(), targetBuffer.data() + sizeof(OpenDB));

  return std::string_view(targetBuffer.data(), messageSize);
}





}}}
