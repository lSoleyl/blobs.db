#pragma once

#include "MemoryBlock.hpp"

namespace blobs::server {

class Blob : public MemoryBlock {
public:
  // Initialize an empty blob
  Blob(blob_id id, commit_id commitId = 1);

  /** Sets the blobs content the specified (primitive value) by simply writing the value in memory representation into the data vector.
   */
  template<typename T>
  void SetIdContent(T value) {
    auto begin = reinterpret_cast<uint8_t*>(&value);
    auto end = begin + sizeof(value);
    data.assign(begin, end);
  }

  /** Setting the blob's content from a std::string_view
   */
  void SetContent(std::string_view blobContent);
  std::string_view ReadContent() const;


  template<typename T>
  T ReadIdContent() const {
    if (data.size() == sizeof(T)) {
      return *reinterpret_cast<T*>(data.data());
    } else {
      // If the blob doesn't actually contain a valid T then we make no attempt at reading it
      return T();
    }
  }

  /** Calculate the size of the Blob's memory block if stored in file
   */
  virtual uint64_t CalculateRequiredSize() const override;

  /** Serialize the blob into a buffer for writing it to file
   */
  virtual void SerializeIntoBuffer(std::vector<char>& targetBuffer) const override;


  const blob_id id;
  const commit_id commitId; // id of commit/transaction when this blob was created/written

private:
  std::vector<uint8_t> data;
};

}