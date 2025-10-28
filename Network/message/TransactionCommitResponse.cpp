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


std::ostream& operator<<(std::ostream& out, const TransactionCommitResponse& message) {
  using Result = TransactionCommitResponse::Result;
  out << message.type << '(';
  switch (message.result) {
    case Result::SUCCESS:
      out << "dbId=" << message.begin()->dbId << ", commitId=" << message.begin()->commitId;
      break;

    case Result::DATABASE_NOT_OPENED:
      out << "DATABASE_NOT_OPENED";
      break;

    case Result::DATABASE_ORDER_VIOLATED:
      out << "DATABASE_ORDER_VIOLATED";
      break;

    case Result::MISSING_WRITE_LOCK:
      out << "MISSING_WRITE_LOCK";
      break;

    case Result::NO_TRANSACTION_IN_PROGRESS:
      out << "NO_TRANSACTION_IN_PROGRESS";
      break;

    case Result::SEGMENT_DOES_NOT_EXIST:
      out << "SEGMENT_DOES_NOT_EXIST";
      break;

    case Result::CLUSTER_DOES_NOT_EXIST:
      out << "CLUSTER_DOES_NOT_EXIST";
      break;

    case Result::BLOB_DOES_NOT_EXIST:
      out << "BLOB_DOES_NOT_EXIST";
      break;

    case Result::ILLEGAL_NEXT_FREE_BLOB_ID:
      out << "ILLEGAL_NEXT_FREE_BLOB_ID";
      break;

    case Result::ILLEGAL_NEXT_FREE_CLUSTER_ID:
      out << "ILLEGAL_NEXT_FREE_CLUSTER_ID";
      break;

    case Result::ILLEGAL_NEXT_FREE_SEGMENT_ID:
      out << "ILLEGAL_NEXT_FREE_SEGMENT_ID";
      break;

    default:
      assert(false);  // missing stringification?
  }

  return out << ')';
}


}}}