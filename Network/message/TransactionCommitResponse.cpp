#include <network/message/TransactionCommitResponse.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {

TransactionCommitResponse::TransactionCommitResponse(Result result, message_size messageSize) : 
  Message(messageSize, TransactionCommitResponse::type), result(result) {}




TransactionCommitResponse::DatabaseCommit* TransactionCommitResponse::begin() {
  return reinterpret_cast<DatabaseCommit*>(reinterpret_cast<uint8_t*>(this) + sizeof(TransactionCommitResponse));
}

TransactionCommitResponse::DatabaseCommit* TransactionCommitResponse::end() {
  return reinterpret_cast<DatabaseCommit*>(reinterpret_cast<uint8_t*>(this) + size);
}

const TransactionCommitResponse::DatabaseCommit* TransactionCommitResponse::begin() const {
  return reinterpret_cast<const DatabaseCommit*>(reinterpret_cast<const uint8_t*>(this) + sizeof(TransactionCommitResponse));
}

const TransactionCommitResponse::DatabaseCommit* TransactionCommitResponse::end() const {
  return reinterpret_cast<const DatabaseCommit*>(reinterpret_cast<const uint8_t*>(this) + size);
}


MessagePointer_T<TransactionCommitResponse> TransactionCommitResponse::Create(int nDatabases) {
  assert(nDatabases - 1 <= std::numeric_limits<database_id>::max()); // 0 is a valid id, so the number of databases can be 1 larger than the maximum id
  message_size messageSize = sizeof(TransactionCommitResponse) + sizeof(DatabaseCommit) * nDatabases;
  return MessagePointer_T<TransactionCommitResponse>(new (new char[messageSize]) TransactionCommitResponse(Result::SUCCESS, messageSize));
}

MessagePointer TransactionCommitResponse::CreateError(Result result) {
  return MessagePointer(new TransactionCommitResponse(result, sizeof(TransactionCommitResponse)));
}


std::ostream& operator<<(std::ostream& out, TransactionCommitResponse::Result result) {
  using Result = TransactionCommitResponse::Result;

  switch (result) {
    case Result::SUCCESS: 
      return out << "SUCCESS";

    case Result::DATABASE_NOT_OPENED: 
      return out << "DATABASE_NOT_OPENED";

    case Result::DATABASE_ORDER_VIOLATED:
      return out << "DATABASE_ORDER_VIOLATED";

    case Result::MISSING_WRITE_LOCK:
      return out << "MISSING_WRITE_LOCK";

    case Result::NO_TRANSACTION_IN_PROGRESS:
      return out << "NO_TRANSACTION_IN_PROGRESS";

    case Result::SEGMENT_DOES_NOT_EXIST:
      return out << "SEGMENT_DOES_NOT_EXIST";

    case Result::CLUSTER_DOES_NOT_EXIST:
      return out << "CLUSTER_DOES_NOT_EXIST";

    case Result::BLOB_DOES_NOT_EXIST:
      return out << "BLOB_DOES_NOT_EXIST";

    case Result::ILLEGAL_NEXT_FREE_BLOB_ID:
      return out << "ILLEGAL_NEXT_FREE_BLOB_ID";

    case Result::ILLEGAL_NEXT_FREE_CLUSTER_ID:
      return out << "ILLEGAL_NEXT_FREE_CLUSTER_ID";

    case Result::ILLEGAL_NEXT_FREE_SEGMENT_ID:
      return out << "ILLEGAL_NEXT_FREE_SEGMENT_ID";

    case Result::CANNOT_COMMIT_MVCC_DATABASE:
      return out << "CANNOT_COMMIT_MVCC_DATABASE";

    default:
      assert(!"Missing serialization for TransactionCommitResponse::Result");
      return out;
  }
}

std::ostream& operator<<(std::ostream& out, const TransactionCommitResponse& message) {
  out << message.type << '(';
  if (message.result == TransactionCommitResponse::Result::SUCCESS) {
    out << "dbId=" << message.begin()->dbId << ", commitId=" << message.begin()->commitId;
  } else {
    out << message.result;
  }

  return out << ')';
}


}}}