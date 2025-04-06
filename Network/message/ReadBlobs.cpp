#include <network/message/ReadBlobs.hpp>


namespace blobs {
namespace network {
namespace message {


ReadBlobs::ReadBlobs(uint8_t databaseId, uint8_t nBlobsRequested, bool writeLock) :
  Message(sizeof(ReadBlobs) + nBlobsRequested * sizeof(BlobAddress), ReadBlobs::type), databaseId(databaseId), nBlobsRequested(nBlobsRequested), writeLock(writeLock) {}

ReadBlobs::BlobAddress* ReadBlobs::begin() {
  return reinterpret_cast<BlobAddress*>(reinterpret_cast<uint8_t*>(this) + sizeof(ReadBlobs));
}

ReadBlobs::BlobAddress* ReadBlobs::end() {
  return reinterpret_cast<BlobAddress*>(reinterpret_cast<uint8_t*>(this) + sizeof(ReadBlobs)) + nBlobsRequested;
}

MessagePointer ReadBlobs::Create(uint8_t databaseId, uint8_t nBlobsRequested, bool writeLock) {
  uint32_t messageSize = sizeof(ReadBlobs) + nBlobsRequested * sizeof(BlobAddress);
  
  auto memory = new char[messageSize];
  // Inplace construct to initialize all header properties and return the message to actually set the 
  // blobs to request through iterator access.
  return MessagePointer(new (memory) ReadBlobs(databaseId, nBlobsRequested, writeLock));
}



}}}