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

const BlobsRead::BlobAddress* BlobsRead::begin() const {
  return reinterpret_cast<const BlobAddress*>(reinterpret_cast<const uint8_t*>(this) + sizeof(BlobsRead));
}
const BlobsRead::BlobAddress* BlobsRead::end() const {
  return reinterpret_cast<const BlobAddress*>(reinterpret_cast<const uint8_t*>(this) + sizeof(BlobsRead)) + nBlobsRequested;
}


MessagePointer_T<BlobsRead> BlobsRead::Create(database_id databaseId, uint8_t nBlobsRequested, bool writeLock) {
  auto messageSize = sizeof(BlobsRead) + nBlobsRequested * sizeof(BlobAddress);
  return MessagePointer_T<BlobsRead>(new (new char[messageSize]) BlobsRead(databaseId, nBlobsRequested, writeLock));
}


std::ostream& operator<<(std::ostream& out, const BlobsRead& message) {
  out
    << message.type << "(db=" << message.databaseId << ", n=" << message.nBlobsRequested
    << ", blob[0]=" << *message.begin() << '@' << message.begin()->ifCommitIdHigher
    << ", write=" << std::boolalpha << message.writeLock << ')';

  return out;
}



}}}