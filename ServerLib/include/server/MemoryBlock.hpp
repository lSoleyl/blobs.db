#pragma once

#include "File.hpp"

namespace blobs::server {


/** A base class struct to use for all classes, whose data is allocated in memory blocks in the database file
 */
struct MemoryBlock {
  MemoryBlock();

  /** This method must be implemented by all structures, which can be allocated inside the file database.
   *  After modifying such a data structure, this method should calculate the new required memory size for storing it.
   */
  virtual uint64_t CalculateRequiredSize() const = 0;

  /** This method should serialize the memory block object into the provided buffer to write it into file.
   */
  virtual void SerializeIntoBuffer(std::vector<char>& targetBuffer) const = 0;

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