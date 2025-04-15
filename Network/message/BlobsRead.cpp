#include <network/message/BlobsRead.hpp>


namespace blobs {
namespace network {
namespace message {


BlobsRead::BlobsRead(database_id databaseId, uint8_t nBlobsRequested, bool writeLock) :
  Message(sizeof(BlobsRead) + nBlobsRequested * sizeof(BlobAddress), BlobsRead::type), databaseId(databaseId), nBlobsRequested(nBlobsRequested), writeLock(writeLock) {}

BlobsRead::BlobAddress* BlobsRead::begin() {
  return reinterpret_cast<BlobAddress*>(reinterpret_cast<uint8_t*>(this) + sizeof(BlobsRead));
}

BlobsRead::BlobAddress* BlobsRead::end() {
  return reinterpret_cast<BlobAddress*>(reinterpret_cast<uint8_t*>(this) + sizeof(BlobsRead)) + nBlobsRequested;
}

MessagePointer_T<BlobsRead> BlobsRead::Create(database_id databaseId, uint8_t nBlobsRequested, bool writeLock) {
  auto messageSize = sizeof(BlobsRead) + nBlobsRequested * sizeof(BlobAddress);
  return MessagePointer_T<BlobsRead>(new (new char[messageSize]) BlobsRead(databaseId, nBlobsRequested, writeLock));
}



}}}