#include "pch.hpp"
#include "include/server/FileBackend.hpp"
#include "include/server/MemoryBlock.hpp"

#include <common/Encoding.hpp>
#include <common/Paths.hpp>

namespace blobs::server {


FileBackend::FileBackend() noexcept : handle(INVALID_HANDLE_VALUE) {}

FileBackend::FileBackend(FileBackend&& other) noexcept : handle(other.handle) {
  other.handle = INVALID_HANDLE_VALUE;
}

FileBackend& FileBackend::operator=(FileBackend&& other) noexcept {
  Close();
  handle = other.handle;
  other.handle = INVALID_HANDLE_VALUE;
  return *this;
}

FileBackend::~FileBackend() noexcept {
  Close();
}


FileBackend FileBackend::OpenExclusive(const char* filePath, bool& exists) {
  
  auto utf16Path = encoding::ToUTF16(filePath);

  // Create the parent directories if necessary
  Paths::MakeDirs(utf16Path, true);


  FileBackend result;
  result.handle = CreateFileW(
    utf16Path.c_str(),
    GENERIC_READ | GENERIC_WRITE,
    0 /*exclusive access*/,
    NULL,
    // for now no FILE_FLAG_OVERLAPPED as we want to perform serial reads/writes in this thread alone
    // We don't specify FILE_FLAG_NO_BUFFERING as it requires us to always read/write whole sectors, which would only complicate things
    // We don't specify FILE_FLAG_WRITE_THROUGH because we only need to flush the writes once we perform the final pointer update (or right before and right after).
    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
    NULL
  );

  exists = (result && GetLastError() == ERROR_ALREADY_EXISTS);
  return result;
}


FileBackend::operator bool() const noexcept {
  return handle != INVALID_HANDLE_VALUE;
}

void FileBackend::Close() noexcept {
  if (*this) {
    CloseHandle(handle);
    handle = INVALID_HANDLE_VALUE;
  }
}


void FileBackend::SetFilePosition(size_t position) const {
  assert(*this); // Any file operation on a closed file is an error
  LARGE_INTEGER offset;
  offset.QuadPart = position;
  SetFilePointerEx(handle, offset, NULL, FILE_BEGIN);
}

void FileBackend::FlushFileBuffers() const {
  ::FlushFileBuffers(handle);
}

bool FileBackend::ReadIntoMemory(void* memory, size_t bytes) const {
  assert(*this); // Reading a closed file is a programming error!

  char* buffer = static_cast<char*>(memory);
  DWORD bytesRead;
  while (bytes > 0) {
    if (!ReadFile(handle, buffer, static_cast<DWORD>(std::min<size_t>(std::numeric_limits<DWORD>::max(), bytes)), &bytesRead, NULL)) {
      // Read failed
      return false;
    }

    bytes -= bytesRead;
    buffer += bytesRead;
  }

  return true;
}


bool FileBackend::WriteFromMemory(const void* memory, size_t size) const {
  assert(*this); // Writing into a closed file is a programming error!

  const char* buffer = static_cast<const char*>(memory);
  while (size > 0) {
    DWORD bytesWritten;
    if (!WriteFile(handle, buffer, static_cast<DWORD>(std::min<size_t>(std::numeric_limits<DWORD>::max(), size)), &bytesWritten, nullptr)) {
      // File write failed
      return false;
    }

    size -= bytesWritten;
    buffer += bytesWritten;
  }
  return true;
}


bool FileBackend::StoreMemoryBlock(const MemoryBlock& memoryBlock, std::vector<char>& writeBuffer) const {
  memoryBlock.SerializeIntoBuffer(writeBuffer);
  SetFilePosition(memoryBlock.fileLocation.offset);
  return WriteFromMemory(writeBuffer.data(), writeBuffer.size());
}





}