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
  out << message.type << '(';
  if (message.result == TransactionCommitResponse::Result::SUCCESS) {
    out << "commitId=" << message.commitId;
  } else if (message.result == TransactionCommitResponse::Result::DATBASE_NOT_OPENED) {
    out << "DATBASE_NOT_OPENED";
  } else {
    assert(false); // missing stringification?
  }

  return out << ')';
}


}}}