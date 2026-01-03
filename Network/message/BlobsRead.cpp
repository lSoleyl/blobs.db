#include <network/message/BlobsRead.hpp>

#include <cassert>


namespace blobs {
namespace network {
namespace message {


BlobsRead::BlobsRead(database_id databaseId, uint8_t nBlobsRequested, LockMode lockMode) :
  Message(sizeof(BlobsRead) + nBlobsRequested * sizeof(BlobAddress), BlobsRead::type), databaseId(databaseId), nBlobsRequested(nBlobsRequested), lockMode(lockMode) {}

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


bool BlobsRead::NeedsWriteLock() const {
  return lockMode != LockMode::Read;
}


MessagePointer_T<BlobsRead> BlobsRead::Create(database_id databaseId, uint8_t nBlobsRequested, LockMode lockMode) {
  auto messageSize = sizeof(BlobsRead) + nBlobsRequested * sizeof(BlobAddress);
  return MessagePointer_T<BlobsRead>(new (new char[messageSize]) BlobsRead(databaseId, nBlobsRequested, lockMode));
}


std::ostream& operator<<(std::ostream& out, BlobsRead::LockMode mode) {
  switch (mode) {
    case BlobsRead::LockMode::Read: return out << "read";
    case BlobsRead::LockMode::Write: return out << "write";
    case BlobsRead::LockMode::Delete: return out << "delete";
  }
  assert(false);
  return out;
}


std::ostream& operator<<(std::ostream& out, const BlobsRead& message) {
  out
    << message.type << "(db=" << static_cast<int>(message.databaseId) << ", n=" << static_cast<int>(message.nBlobsRequested)
    << ", blob[0]=" << *message.begin()
  ;

  if (message.lockMode != BlobsRead::LockMode::Delete) {
    // Delete messages don't initialize this field as they aren't interested in the content of the blob anyway
    out << '@' << message.begin()->ifCommitIdHigher;
  }
  
  return out << ", access=" << message.lockMode << ')';
}



}}}