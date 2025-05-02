#include <network/message/TransactionCommit.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {


TransactionCommit::TransactionCommit(database_id databaseId, message_size messageSize, uint16_t nBlobsCommitted, bool hasFollowMessage) : 
  Message(messageSize, TransactionCommit::type), databaseId(databaseId), nBlobsCommitted(nBlobsCommitted), hasFollowMessage(hasFollowMessage) {}

MessagePointer_T<TransactionCommit> TransactionCommit::Create(database_id databaseId, size_t totalBlobsSize, uint16_t nBlobsCommitted, bool hasFollowMessage) {
  size_t totalMessageSize = sizeof(TransactionCommit) + nBlobsCommitted * sizeof(BlobData) + totalBlobsSize;
  assert(totalMessageSize <= std::numeric_limits<message_size>::max()); // Make sure the message is large enough to hold the blob data
  return MessagePointer_T<TransactionCommit>(new (new char[totalMessageSize]) TransactionCommit(databaseId, static_cast<message_size>(totalMessageSize), nBlobsCommitted, hasFollowMessage));
}



TransactionCommit::BlobDataIterator TransactionCommit::begin() {
  auto headerBegin = reinterpret_cast<char*>(this) + sizeof(TransactionCommit);
  auto headerEnd = headerBegin + nBlobsCommitted * sizeof(BlobData);
  return BlobDataIterator(reinterpret_cast<BlobData*>(headerBegin), headerEnd);
}

TransactionCommit::BlobDataIterator TransactionCommit::end() {
  auto headerBegin = reinterpret_cast<char*>(this) + sizeof(TransactionCommit);
  auto headerEnd = headerBegin + nBlobsCommitted * sizeof(BlobData);
  auto dataEnd = reinterpret_cast<char*>(this) + size;
  return BlobDataIterator(reinterpret_cast<BlobData*>(headerEnd), dataEnd);
}

TransactionCommit::BlobDataIterator::BlobDataIterator(BlobData* header, char* dataPos) : header(header), dataPos(dataPos) {}

TransactionCommit::BlobDataIterator& TransactionCommit::BlobDataIterator::operator++() {
  // Adjust the data position accordingly while handling the special case of a delete blob size, which is just a marker for deleting the blob
  // during the commit, but transmits no actual data.
  auto actualBlobSize = (header->blobSize == constants::DeleteBlobSize) ? 0 : header->blobSize;
  dataPos += actualBlobSize;
  ++header;
  return *this;
}

TransactionCommit::BlobData& TransactionCommit::BlobDataIterator::operator*() const {
  return *header;
}

TransactionCommit::BlobData* TransactionCommit::BlobDataIterator::operator->() const {
  return header;
}

bool TransactionCommit::BlobDataIterator::operator==(const BlobDataIterator& other) const {
  return header == other.header;
}

std::string_view TransactionCommit::BlobDataIterator::ReadData() const {
  return std::string_view(dataPos, header->blobSize);
}

void TransactionCommit::BlobDataIterator::WriteData(std::string_view data) {
  std::copy_n(data.begin(), std::min<size_t>(data.size(), header->blobSize), dataPos);
}


}}}