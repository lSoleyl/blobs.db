#include <network/message/BlobsReadResponse.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {


BlobsReadResponse::BlobsReadResponse(message_size messageSize, uint8_t nBlobs) : 
   Message(messageSize, BlobsReadResponse::type), result(Result::SUCCESS), nBlobs(nBlobs) {}

BlobsReadResponse::BlobsReadResponse(Result result) : Message(sizeof(BlobsReadResponse), BlobsReadResponse::type), result(result), nBlobs(0) {}

MessagePointer_T<BlobsReadResponse> BlobsReadResponse::Create(size_t totalBlobsSize, uint8_t nBlobs) {
  auto messageSize = sizeof(BlobsReadResponse) + sizeof(BlobData) * nBlobs + totalBlobsSize;
  assert(messageSize <= std::numeric_limits<message_size>::max()); // Make sure the message is large enough to hold the blob data

  return MessagePointer_T<BlobsReadResponse>(new (new char[messageSize]) BlobsReadResponse(static_cast<message_size>(messageSize), nBlobs));
}

MessagePointer_T<BlobsReadResponse> BlobsReadResponse::CreateError(Result result) {
  return MessagePointer_T<BlobsReadResponse>(new BlobsReadResponse(result));
}


const void* BlobsReadResponse::BlobData::Data() const {
  return reinterpret_cast<const char*>(this) + sizeof(BlobData);
}

BlobsReadResponse::Iterator::Iterator(void* payloadPos) : pos(payloadPos) {}

void BlobsReadResponse::Iterator::SetBlob(const BlobLocation& location, commit_id commitId, const void* data, blob_size size) {
  auto& header = **this;
  header = location;
  header.blobSize = size;
  header.commitId = commitId;
  std::copy_n(static_cast<const char*>(data), size, static_cast<char*>(pos) + sizeof(BlobData));
}

void BlobsReadResponse::Iterator::operator++() {
  pos = static_cast<char*>(pos) + sizeof(BlobData) + (**this).blobSize;
}

BlobsReadResponse::BlobData& BlobsReadResponse::Iterator::operator*() const {
  return *static_cast<BlobData*>(pos);
}

bool BlobsReadResponse::Iterator::operator==(const Iterator& other) const { 
  return pos == other.pos; 
}
bool BlobsReadResponse::Iterator::operator!=(const Iterator & other) const {
  return pos != other.pos; 
}


BlobsReadResponse::Iterator BlobsReadResponse::begin() {
  return Iterator(reinterpret_cast<char*>(this) + sizeof(BlobsReadResponse));
}

BlobsReadResponse::Iterator BlobsReadResponse::end() {
  return Iterator(reinterpret_cast<char*>(this) + size);
}

}}}
