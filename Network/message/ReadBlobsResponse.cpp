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


std::string_view BlobsReadResponse::BlobData::Content() const {
  return std::string_view(reinterpret_cast<const char*>(this) + sizeof(BlobData), blobSize);
}

BlobsReadResponse::Iterator::Iterator(void* payloadPos) : pos(payloadPos) {}

void BlobsReadResponse::Iterator::SetBlob(segment_id segment, cluster_id cluster, blob_id blob, commit_id commitId, void* data, blob_size size) {
  auto& header = **this;
  header.segment = segment;
  header.cluster = cluster;
  header.blob = blob;
  header.blobSize = size;
  header.commitId = commitId;
  std::copy_n(static_cast<char*>(data), size, static_cast<char*>(pos) + sizeof(BlobData));
}

void BlobsReadResponse::Iterator::operator++() {
  pos = static_cast<char*>(pos) + sizeof(BlobData) + (**this).blobSize;
}

BlobsReadResponse::BlobData& BlobsReadResponse::Iterator::operator*() const {
  return *static_cast<BlobData*>(pos);
}

}}}
