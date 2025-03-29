#pragma once

#include "Message.hpp"
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

  /** Encode the OpenDB message into the given target buffer, which will be resized if necessary. 
   *  The returned string_view will contain the byte range of the buffer, which makes up this message.
   */
  static std::string_view EncodeMessage(std::vector<char>& targetBuffer, std::string_view databaseName);


private:
  OpenDB(); // Do not use the constructor -> use EncodeMessage
};




}}}