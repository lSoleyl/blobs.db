#include <network/message/TransactionCommitResponse.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {

TransactionCommitResponse::TransactionCommitResponse(Result result, commit_id commitId) : 
  Message(sizeof(TransactionCommitResponse), TransactionCommitResponse::type), result(result), commitId(commitId) {}



MessagePointer TransactionCommitResponse::Create(commit_id commitId) {
  return MessagePointer(new TransactionCommitResponse(Result::SUCCESS, commitId));
}

MessagePointer TransactionCommitResponse::CreateError(Result result) {
  return MessagePointer(new TransactionCommitResponse(result, 0));
}

std::ostream& operator<<(std::ostream& out, const TransactionCommitResponse& message) {
  using Result = TransactionCommitResponse::Result;
  out << message.type << '(';
  switch (message.result) {
    case Result::SUCCESS:
      out << "commitId=" << message.commitId;
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