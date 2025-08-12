#pragma once

#include "File.hpp"

namespace blobs::server {


/** A base class struct to use for all classes, whose data is allocated in memory blocks in the database file
 */
struct MemoryBlock {
  MemoryBlock();

  /** Where in the file this object is allcoated
   */
  file::BlockReference fileLocation;

  enum class Status : uint8_t {
    NOT_LOADED,
    LOADING,
    LOADED
  };

  Status status;
};






}