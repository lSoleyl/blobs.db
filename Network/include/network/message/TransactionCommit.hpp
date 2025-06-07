#pragma once

#include "..\MessagePointer.hpp"
#include "..\..\common\BlobLocation.hpp"

namespace blobs {
namespace network {
namespace message {


/** Send by the client to commit a currently running transaction and write back all changed blobs to the server and release all locks.
 *  The client will receive a TransactionCommitResponse message to notify him about the transaction result. 
 * 
 *  Due to the potentially large amount of blobs to transfer and to support multiple databases, this message has a follow flag, which (if set) will
 *  indicate that a following TransactionCommit message belongs to the same commit.
 */
struct TransactionCommit : public Message {
  bool hasFollowMessage;    // true if this is a partial commit, which is followed by at least one more commit message
  database_id databaseId;   // id referencing the previously opened database to read from
  uint16_t nBlobsCommitted; // number of blobs to write back to the database (this also corresponds the number of BlobData headers)

  struct BlobData : public BlobLocation {
    using BlobLocation::operator=; // allow assignment from BlobLocation
    blob_size blobSize;
  };

  /** An iterator used to access the BlobData header and write the blob's data into the blobs location.
   */
  class BlobDataIterator {
  public:
    BlobDataIterator(BlobData* header, char* dataPos);

    BlobDataIterator& operator++();
    BlobData& operator*() const;
    BlobData* operator->() const;

    bool operator==(const BlobDataIterator& other) const;
    bool operator!=(const BlobDataIterator& other) const;

    std::string_view ReadData() const;
    void WriteData(std::string_view data);


    /** A helper to read blobs, which only contain an id like the nextFreeBlobId blob
     *  No size/type check is performed here this must be validated before calling this method.
     */
    template<typename IdType>
    IdType ReadId() const {
      return *reinterpret_cast<const IdType*>(ReadData().data());
    }

  private:
    BlobData* header;
    char* dataPos; // needed to keep track of the write position of the blob data
  };

  /** Begin and end iterators for accessing blob headers and their data
   */
  BlobDataIterator begin();
  BlobDataIterator end();


  /** Constructs a new TransactionCommit message with sufficient size to hold the blobs to commit
   *  The caller is responsible for not requesting a message larger than the max message_size
   */
  static MessagePointer_T<TransactionCommit> Create(database_id databaseId, size_t totalBlobsSize, uint16_t nBlobsCommitted, bool hasFollowMessage = false);

  static constexpr Type type = Type::TransactionCommit;
private:
  TransactionCommit(database_id databaseId, message_size messageSize, uint16_t nBlobsCommitted, bool hasFollowMessage);
};


}}}