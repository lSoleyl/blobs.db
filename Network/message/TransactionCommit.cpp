#include <network/message/TransactionCommit.hpp>
#include <cassert>

namespace blobs {
namespace network {
namespace message {


TransactionCommit::TransactionCommit(database_id databaseId, message_size messageSize, uint16_t nBlobsCommitted, bool hasFollowMessage) : 
  Message(messageSize, TransactionCommit::type), databaseId(databaseId), nBlobsCommitted(nBlobsCommitted), hasFollowMessage(hasFollowMessage) {}

MessagePointer_T<TransactionCommit> TransactionCommit::Create(database_id databaseId, size_t totalBlobsSize, size_t nBlobsCommitted, bool hasFollowMessage) {
  auto totalMessageSize = CalculateMessageSize(totalBlobsSize, nBlobsCommitted);
  assert(totalMessageSize != 0); // Make sure the message is large enough to hold the blob data (0 is returned if the data is too large for the message)
  return MessagePointer_T<TransactionCommit>(new (new char[totalMessageSize]) TransactionCommit(databaseId, totalMessageSize, nBlobsCommitted, hasFollowMessage));
}


message_size TransactionCommit::CalculateMessageSize(size_t totalBlobsSize, size_t nBlobsCommitted) {
  if (nBlobsCommitted > std::numeric_limits<uint16_t>::max()) {
    // We cannot transmit more than 65k blobs in one message
    return 0;
  }

  size_t totalMessageSize = sizeof(TransactionCommit) + nBlobsCommitted * sizeof(BlobData) + totalBlobsSize;
  return (totalMessageSize <= std::numeric_limits<message_size>::max()) ? static_cast<message_size>(totalMessageSize) : 0;
}

bool TransactionCommit::IsValidMessageSize(size_t totalBlobsSize, size_t nBlobsCommitted) {
  return CalculateMessageSize(totalBlobsSize, nBlobsCommitted) != 0;
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

bool TransactionCommit::BlobDataIterator::operator!=(const BlobDataIterator& other) const {
  return header != other.header;
}

std::string_view TransactionCommit::BlobDataIterator::ReadData() const {
  return std::string_view(dataPos, header->blobSize);
}

void TransactionCommit::BlobDataIterator::WriteData(std::string_view data) {
  std::copy_n(data.begin(), std::min<size_t>(data.size(), header->blobSize), dataPos);
}


}}}