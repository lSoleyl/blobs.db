#include <network/message/TransactionBeginResponse.hpp>

#include <cassert>

namespace blobs::network::message {


TransactionBeginResponse::TransactionBeginResponse(Result result, message_size messageSize) : Message(messageSize, TransactionBeginResponse::type), result(result) {}



BlobLocation* TransactionBeginResponse::DatabaseStickyLocks::begin() {
  return reinterpret_cast<BlobLocation*>(this + 1);
}

BlobLocation* TransactionBeginResponse::DatabaseStickyLocks::end() {
  return begin() + nLocks;
}


TransactionBeginResponse::iterator::iterator(DatabaseStickyLocks* pos) : pos(pos) {}

auto TransactionBeginResponse::iterator::operator*() const -> reference {
  return *pos;
}

auto TransactionBeginResponse::iterator::operator->() const -> pointer {
  return pos;
}

auto TransactionBeginResponse::iterator::operator++() -> iterator& {
  pos = reinterpret_cast<pointer>(reinterpret_cast<uint8_t*>(pos) + sizeof(DatabaseStickyLocks) + pos->nLocks * sizeof(BlobLocation));
  return *this;
}

bool TransactionBeginResponse::iterator::operator==(const iterator& other) const {
  return pos == other.pos;
}

bool TransactionBeginResponse::iterator::operator!=(const iterator& other) const {
  return pos != other.pos;
}



auto TransactionBeginResponse::begin() -> iterator {
  return iterator(reinterpret_cast<DatabaseStickyLocks*>(reinterpret_cast<uint8_t*>(this) + sizeof(TransactionBeginResponse)));
}

auto TransactionBeginResponse::end() -> iterator {
  return iterator(reinterpret_cast<DatabaseStickyLocks*>(reinterpret_cast<uint8_t*>(this) + size));
}


bool TransactionBeginResponse::IsSuccess() const {
  return result == Result::SUCCESS;
}

MessagePointer_T<TransactionBeginResponse> TransactionBeginResponse::Create(uint32_t nDatabases, uint16_t nLocks) {
  assert(nDatabases <= std::numeric_limits<database_id>::max()); // Programming error if the server thinks the client has more than databases open than allowed
  auto messageSize = sizeof(TransactionBeginResponse) + nDatabases * sizeof(DatabaseStickyLocks) + nLocks * sizeof(BlobLocation);
  return MessagePointer_T<TransactionBeginResponse>(new (new char[messageSize]) TransactionBeginResponse(Result::SUCCESS, messageSize));
}


MessagePointer TransactionBeginResponse::CreateError(Result result) {
  return MessagePointer(new TransactionBeginResponse(result, sizeof(TransactionBeginResponse)));
}


std::ostream& operator<<(std::ostream& out, TransactionBeginResponse::Result result) {
  switch (result) {
    case TransactionBeginResponse::Result::SUCCESS: return out << "SUCCESS";
    case TransactionBeginResponse::Result::ERROR_ALREADY_IN_TRANSACTION: return out << "ERROR_ALREADY_IN_TRANSACTION";
    case TransactionBeginResponse::Result::ERROR_INVALID_DATABASE: return out << "ERROR_INVALID_DATABASE";
  }
  assert(false);
  return out;
}


std::ostream& operator<<(std::ostream& out, const TransactionBeginResponse& message) {
  out << message.type << "(result=" << message.result;
  if (message.IsSuccess()) {
    for (auto& dbLocks : const_cast<TransactionBeginResponse&>(message)) { // const_cast is safe as we will only read
      out << ", {db=" << static_cast<int>(dbLocks.databaseId) << ", " << (dbLocks.keep ? "kept=" : "released=") << dbLocks.nLocks << '}';
    }
  }

  return out << ')';
}



}