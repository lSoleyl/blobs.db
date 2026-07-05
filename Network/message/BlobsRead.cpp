#include <network/message/BlobsRead.hpp>

#include <cassert>


namespace blobs {
namespace network {
namespace message {


BlobsRead::BlobsRead(database_id databaseId, uint8_t nBlobsRequested, LockMode lockMode, int32_t lockTimeoutMs) :
  Message(sizeof(BlobsRead) + nBlobsRequested * sizeof(BlobAddress), BlobsRead::type), databaseId(databaseId), nBlobsRequested(nBlobsRequested), lockMode(lockMode), lockTimeoutMs(lockTimeoutMs) {}

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
  // None and Read do NOT need a write lock
  return lockMode == LockMode::Write || lockMode == LockMode::Delete;
}

bool BlobsRead::IsDirtyRead() const {
  return lockMode == LockMode::None;
}

MessagePointer_T<BlobsRead> BlobsRead::Create(database_id databaseId, uint8_t nBlobsRequested, LockMode lockMode, int32_t lockTimeoutMs) {
  auto messageSize = sizeof(BlobsRead) + nBlobsRequested * sizeof(BlobAddress);
  return MessagePointer_T<BlobsRead>(new (new char[messageSize]) BlobsRead(databaseId, nBlobsRequested, lockMode, lockTimeoutMs));
}


std::ostream& operator<<(std::ostream& out, BlobsRead::LockMode mode) {
  switch (mode) {
    case BlobsRead::LockMode::None: return out << "dirty_read";
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
    out << '@' << message.begin()->cacheCommitId;
  }
  
  out << ", access=" << message.lockMode;
  if (message.lockTimeoutMs >= 0) {
    // Log the timeout property if a timeout is configured
    out << ", timeout=" << message.lockTimeoutMs << "ms";
  }
  
  return out << ')';
}



}}}