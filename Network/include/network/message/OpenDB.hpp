#pragma once

#include "..\MessagePointer.hpp"
#include <string_view>
#include <vector>

namespace blobs {
namespace network {
namespace message {



struct OpenDB : public Message {
  // Maybe we will add some additional data here like open flags and other things... 
  // Everything following these flags will simply be the database name. We don't have to encode its length
  // because we can simply calculate it from the message length
  std::string_view GetDatabaseName() const;

  /** Encode the OpenDB message and return a pointer to the message's memory
   */
  static MessagePointer Create(std::string_view databaseName);

  static constexpr Type type = Type::OpenDB;
private:
  OpenDB(uint32_t messageSize); // Do not use the constructor -> use EncodeMessage
};




}}}