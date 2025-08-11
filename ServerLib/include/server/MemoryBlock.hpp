#pragma once

namespace blobs::server {


/** A base class struct to use for all classes, whose data is allocated in memory blocks in the database file
 */
struct MemoryBlock {
  MemoryBlock();

  uint64_t offset;
  uint64_t size;

  enum class Status : uint8_t {
    NOT_LOADED,
    LOADING,
    LOADED
  };

  Status status;
};






}