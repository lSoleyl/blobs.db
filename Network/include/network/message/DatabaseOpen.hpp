#pragma once

#include "..\MessagePointer.hpp"
#include <string_view>

namespace blobs {
namespace network {
namespace message {



struct DatabaseOpen : public Message {
  // Maybe we will add some additional data here like open flags and other things... 
  // Everything following these flags will simply be the database name. We don't have to encode its length
  // because we can simply calculate it from the message length
  std::string_view GetDatabaseName() const;

  /** Encode the DatabaseOpen message and return a pointer to the message's memory
   */
  static MessagePointer Create(std::string_view databaseName);

  static constexpr Type type = Type::DatabaseOpen;
private:
  DatabaseOpen(uint32_t messageSize, std::string_view databaseName); // Do not use the constructor -> use EncodeMessage
};




}}}