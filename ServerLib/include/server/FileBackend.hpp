#pragma once

#include "File.hpp"
#include "win_include.hpp"

namespace blobs::server {

struct MemoryBlock;

/** This class wraps all access to a native file handle and provides
 *  common methods for loading storing file::BlockReference objects.
 */
class FileBackend {
public:
  FileBackend() noexcept;
  FileBackend(FileBackend&& other) noexcept;
  FileBackend& operator=(FileBackend&& other) noexcept;

  /** Calls Close()
   */
  ~FileBackend() noexcept;

  /** Opens the file at the specified path for exclusive RW access and returns
   *  the file backend object. 
   *  
   * @param filePath the file to open
   * @param exists out parameter, which is set to true if the file alredy exists
   * 
   * @return the file backend or an uninitialized file backend if opening the file failed
   */
  static FileBackend OpenExclusive(const char* filePath, bool& exists);

  /** Evaluates to true if file handle is initialized
   */
  explicit operator bool() const noexcept;

  /** Closes the file (if it was opened before)
   */
  void Close() noexcept;


  /** Moves the file pointer to the given absolute file position
   */
  void SetFilePosition(size_t position) const;

  /** Flush the file write buffers to disk
   */
  void FlushFileBuffers() const;


  /** Reads the file at the current location and returns if the specified buffer could successfully be filled with the requested amount of bytes from the file
   *  This method will perform multiple reads if necessary
   */
  bool ReadIntoMemory(void* memory, size_t bytes) const;

  /** A helper method to read an arbitrary struct's data from the current file location
   */
  template<typename T>
  bool ReadStruct(T& targetStruct) const {
    return ReadIntoMemory(&targetStruct, sizeof(T));
  }

  /** Helper method to load a memory block reference of the specified type from the file.
   */
  template<typename T>
  std::unique_ptr<T> LoadMemoryBlock(const file::BlockReference& reference) const {
    std::unique_ptr<char[]> buffer(new char[reference.size]);
    SetFilePosition(reference.offset);
    if (ReadIntoMemory(buffer.get(), reference.size)) {
      return std::unique_ptr<T>(reinterpret_cast<T*>(buffer.release()));
    } else {
      // Otherwise we failed to read the specified amount of bytes -> return a nullptr to indicate that error
      return std::unique_ptr<T>();
    }
  }


  /** Writes the specified buffer into the specified file at the file's current position.
   *  This method will perform multiple writes if necessary
   * 
   * @param memory pointer to the memory to write
   * @param size of the memory to write (in bytes)
   * 
   * @return true if successful, false if writing failed
   */
  bool WriteFromMemory(const void* memory, size_t size) const;

  /** A helper method to write the given structure into the file at the current file position
   */
  template<typename T>
  bool WriteStruct(const T& sourceStruct) const {
    return WriteFromMemory(&sourceStruct, sizeof(T));
  }

  /** Writes the given memory block into the file at the location specified in the memory block
   * 
   * @param memoryBlock the block to write to file
   * @param writeBuffer a vector to reuse as write buffer (memoryBlock is serialized in here)
   * 
   * @return true if successful
   */
  bool StoreMemoryBlock(const MemoryBlock& memoryBlock, std::vector<char>& writeBuffer) const;


  

private:
  // Not copyable
  FileBackend(const FileBackend&) = delete;
  FileBackend& operator=(const FileBackend&) = delete;


  HANDLE handle;
};

}