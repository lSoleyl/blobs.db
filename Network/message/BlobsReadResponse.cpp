#include <network/message/BlobsReadResponse.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {


static_assert(sizeof(BlobsReadResponse) + sizeof(BlobsReadResponse::BlobData) <= constants::_BlobMessageSize, "constants::_BlobMessageSize is too small!");

BlobsReadResponse::BlobsReadResponse(message_size messageSize, uint8_t nBlobs) : 
   Message(messageSize, BlobsReadResponse::type), result(Result::SUCCESS), nBlobs(nBlobs), hasFollowMessage(false) {}

BlobsReadResponse::BlobsReadResponse(message_size messageSize, Result result, std::string_view errorDetails) : 
  Message(messageSize, BlobsReadResponse::type), result(result), nBlobs(0), hasFollowMessage(false)
{
  // Copy the error details into the memory PAST the message (the caller of the constructor must make sure to allocate the sufficient memory for this operation
  std::copy(errorDetails.begin(), errorDetails.end(), reinterpret_cast<char*>(this + 1));
}


bool BlobsReadResponse::FitsIntoMessage(size_t totalBlobsSize, uint8_t nBlobs) {
  auto messageSize = sizeof(BlobsReadResponse) + sizeof(BlobData) * nBlobs + totalBlobsSize;
  return messageSize <= std::numeric_limits<message_size>::max();
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

BlobsReadResponse::Iterator::Iterator(BlobData* pos, void* dataPos) : pos(pos), dataPos(dataPos) {}

void BlobsReadResponse::Iterator::SetBlob(const BlobLocation& location, commit_id commitId, const void* data, blob_size size) {
  auto& header = **this;
  header = location;
  header.blobSize = size;
  header.commitId = commitId;
  std::memcpy(dataPos, data, size);
}

const void* BlobsReadResponse::Iterator::GetData() const {
  return dataPos;
}

void BlobsReadResponse::Iterator::operator++() {
  // First move dataPos to the next free spot and only then update the blob data pointer
  dataPos = static_cast<char*>(dataPos) + pos->blobSize;
  ++pos;
}

BlobsReadResponse::BlobData& BlobsReadResponse::Iterator::operator*() const {
  return *static_cast<BlobData*>(pos);
}

BlobsReadResponse::BlobData* BlobsReadResponse::Iterator::operator->() const {
  return static_cast<BlobData*>(pos);
}

bool BlobsReadResponse::Iterator::operator==(const Iterator& other) const { 
  return pos == other.pos; 
}
bool BlobsReadResponse::Iterator::operator!=(const Iterator & other) const {
  return pos != other.pos; 
}


BlobsReadResponse::Iterator BlobsReadResponse::begin() {
  // BlobData entries start right after the BlobsReadResponse and the actual blob data starts after the last blob data entry
  auto blobDataBegin = reinterpret_cast<BlobData*>(this + 1);
  auto blobDataEnd = blobDataBegin + nBlobs;
  return Iterator(blobDataBegin, blobDataEnd);
}

BlobsReadResponse::Iterator BlobsReadResponse::end() {
  // BlobData entries start right after the BlobsReadResponse and the actual blob data starts after the last blob data entry
  auto blobDataBegin = reinterpret_cast<BlobData*>(this + 1);
  auto blobDataEnd = blobDataBegin + nBlobs;
  return Iterator(blobDataEnd, blobDataEnd);
}

}}}
