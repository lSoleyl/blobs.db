#pragma once

#include "..\MessagePointer.hpp"
#include "..\..\common\BlobLocation.hpp"

#include <string_view>

namespace blobs {
namespace network {
namespace message {

/** The server's reply to a BlobsRead message
 */
struct BlobsReadResponse : public Message {
  enum class Result : uint8_t {
    SUCCESS,
    BLOB_DOES_NOT_EXIST,
    DATBASE_NOT_OPENED,         // passed a database id, which the server doesn't recognize for this client
    NO_TRANSACTION_IN_PROGRESS, // trying to read blobs before explicitly starting a transaction
    LOCK_TIMEOUT,
    DEADLOCK                    // deadlocked with another client. The error message is allocated as string following this message.
  };

  Result result;
  uint8_t nBlobs; // number of blobs replied

  FIXME("The blob data should not be stored right after the header as this can result in non-aligned reads for following blob headers!");

  struct BlobData : public BlobLocation {
    using BlobLocation::operator=; // allow assignment from BlobLocation

    blob_size blobSize;
    commit_id commitId;

    /** Used to read the blob's data when processing the message on the client
     */
    const void* Data() const;
  };

  /** Creates a new BlobsReadResponse message allocating enough memory to transmit the specified number of 
   *  blobs with the specified total blob size
   */
  static MessagePointer_T<BlobsReadResponse> Create(size_t totalBlobsSize, uint8_t nBlobs = 1);

  /** Creates an empty error response with only the Result value set.
   *  If an error details message is supplied, it will be allocated behind the message and may be displayed by the client.
   */
  static MessagePointer_T<BlobsReadResponse> CreateError(Result result, std::string_view errorDetails = "");

  /** Returns the error details string for this error message.
   */
  std::string_view GetErrorDetails() const;

  /** This class is used to efficiently write/read the blob data into/from this message
   */
  class Iterator {
  public:
    Iterator(void* payloadPos);
    void SetBlob(const BlobLocation& location, commit_id commitId, const void* data, blob_size size);
    void operator++(); // only increment AFTER setting the blob so the iterator knows how far to increment

    // Read acccess to the blob data header
    BlobData& operator*() const;
    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;
  private:
    void* pos;
  };

  Iterator begin();
  Iterator end();



  static constexpr Type type = Type::BlobsReadResponse;
private:
  BlobsReadResponse(message_size messageSize, uint8_t nBlobs); 
  BlobsReadResponse(message_size messageSize, Result result, std::string_view errorDetails);
};


}}}
