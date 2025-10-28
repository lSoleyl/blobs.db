#include <network/message/TransactionBeginResponse.hpp>

#include <cassert>

namespace blobs::network::message {


TransactionBeginResponse::TransactionBeginResponse(Result result, message_size messageSize) : Message(messageSize, TransactionBeginResponse::type), result(result) {}


BlobLocation* TransactionBeginResponse::begin() {
  return reinterpret_cast<BlobLocation*>(reinterpret_cast<uint8_t*>(this) + sizeof(TransactionBeginResponse));
}

BlobLocation* TransactionBeginResponse::end() {
  return reinterpret_cast<BlobLocation*>(reinterpret_cast<uint8_t*>(this) + size);
}

const BlobLocation* TransactionBeginResponse::begin() const {
  return reinterpret_cast<const BlobLocation*>(reinterpret_cast<const uint8_t*>(this) + sizeof(TransactionBeginResponse));
}
const BlobLocation* TransactionBeginResponse::end() const {
  return reinterpret_cast<const BlobLocation*>(reinterpret_cast<const uint8_t*>(this) + size);
}


bool TransactionBeginResponse::IsSuccess() const {
  return result == Result::SUCCESS_KEEP_LOCKS || result == Result::SUCCESS_RELEASE_LOCKS;
}

MessagePointer_T<TransactionBeginResponse> TransactionBeginResponse::Create(bool keepLocks, uint16_t nLocks) {
  auto messageSize = sizeof(TransactionBeginResponse) + nLocks * sizeof(BlobLocation);
  auto result = keepLocks ? Result::SUCCESS_KEEP_LOCKS : Result::SUCCESS_RELEASE_LOCKS;
  return MessagePointer_T<TransactionBeginResponse>(new (new char[messageSize]) TransactionBeginResponse(result, messageSize));
}


MessagePointer TransactionBeginResponse::CreateError(Result result) {
  return MessagePointer(new TransactionBeginResponse(result, sizeof(TransactionBeginResponse)));
}


std::ostream& operator<<(std::ostream& out, TransactionBeginResponse::Result result) {
  switch (result) {
    case TransactionBeginResponse::Result::SUCCESS_KEEP_LOCKS: return out << "SUCCESS_KEEP_LOCKS";
    case TransactionBeginResponse::Result::SUCCESS_RELEASE_LOCKS: return out << "SUCCESS_RELEASE_LOCKS";
    case TransactionBeginResponse::Result::ERROR_ALREADY_IN_TRANSACTION: return out << "ERROR_ALREADY_IN_TRANSACTION";
  }
  assert(false);
  return out;
}


std::ostream& operator<<(std::ostream& out, const TransactionBeginResponse& message) {
  out << message.type << "(result=" << message.result;
  if (message.IsSuccess()) {
    out << ", nLocks=" << std::distance(message.begin(), message.end());
  }

  return out << ')';
}



}