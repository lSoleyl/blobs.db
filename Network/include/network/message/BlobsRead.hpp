#pragma once

#include "..\MessagePointer.hpp"
#include "..\..\common\BlobLocation.hpp"

#include <string_view>
#include <vector>

namespace blobs {
namespace network {
namespace message {

/** Read (and lock) 1-255 blobs at once
 */
struct BlobsRead : public Message {
  database_id databaseId; // id referencing the previously opened database to read from
  uint8_t nBlobsRequested; // value 0 is not valid here
  bool writeLock; // if true, the blobs will be write locked, otherwise just read locked

  // One such following structure is written into the message for each requested blob
  // The requested blobs should already be sorted in ascending order to reduce the risk of deadlocks
  struct BlobAddress : public BlobLocation {
    using BlobLocation::operator=; // allow assignment from BlobLocation
    commit_id ifCommitIdHigher; // Only return the blob contents if the commit id is higher than specified here
  };

  /** STL like iteration over each single blob request
   */
  BlobAddress* begin();
  BlobAddress* end();

  const BlobAddress* begin() const;
  const BlobAddress* end() const;


  /** Create a new OpenDB message with sufficient space to hold nBlobsRequested number of blobs.
   *  The BlobAddresses are at this point not initialized and have to be initialized using the begin()/end() iterators
   */
  static MessagePointer_T<BlobsRead> Create(database_id databaseId, uint8_t nBlobsRequested = 1, bool writeLock = false);


  static constexpr Type type = Type::BlobsRead;
private:
  BlobsRead(database_id databaseId, uint8_t nBlobsRequested, bool writeLock); // Do not use the constructor -> use EncodeMessage
};


std::ostream& operator<<(std::ostream& out, const BlobsRead& message);


}}}