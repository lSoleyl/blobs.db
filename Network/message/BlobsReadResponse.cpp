#include <network/message/BlobsReadResponse.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {


static_assert(sizeof(BlobsReadResponse) + sizeof(BlobsReadResponse::BlobData) <= constants::_BlobMessageSize, "constants::_BlobMessageSize is too small!");

BlobsReadResponse::BlobsReadResponse(message_size messageSize, uint8_t nBlobs) : 
   Message(messageSize, BlobsReadResponse::type), result(Result::SUCCESS), nBlobs(nBlobs) {}

BlobsReadResponse::BlobsReadResponse(message_size messageSize, Result result, std::string_view errorDetails) : 
  Message(messageSize, BlobsReadResponse::type), result(result), nBlobs(0) 
{
  // Copy the error details into the memory PAST the message (the caller of the constructor must make sure to allocate the sufficient memory for this operation
  std::copy(errorDetails.begin(), errorDetails.end(), reinterpret_cast<char*>(this + 1));
}

MessagePointer_T<BlobsReadResponse> BlobsReadResponse::Create(size_t totalBlobsSize, uint8_t nBlobs) {
  auto messageSize = sizeof(BlobsReadResponse) + sizeof(BlobData) * nBlobs + totalBlobsSize;
  assert(messageSize <= std::numeric_limits<message_size>::max()); // Make sure the message is large enough to hold the blob data

  return MessagePointer_T<BlobsReadResponse>(new (new char[messageSize]) BlobsReadResponse(static_cast<message_size>(messageSize), nBlobs));
}

MessagePointer_T<BlobsReadResponse> BlobsReadResponse::CreateError(Result result, std::string_view errorDetails) {
  auto messageSize = sizeof(BlobsReadResponse) + errorDetails.size();
  assert(messageSize <= std::numeric_limits<message_size>::max()); // Make sure the message is large enough to hold the error response

  return MessagePointer_T<BlobsReadResponse>(new (new char[messageSize]) BlobsReadResponse(static_cast<message_size>(messageSize), result, errorDetails));
}

std::string_view BlobsReadResponse::GetErrorDetails() const {
  // The error details are just like the data allocated behind the message in memory
  assert(result != Result::SUCCESS); // Cannot retrieve the error details for a successful read
  return std::string_view(reinterpret_cast<const char*>(this+1), size - sizeof(BlobsReadResponse));
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
