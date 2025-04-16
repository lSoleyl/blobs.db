#pragma once

#include "..\MessagePointer.hpp"
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
    LOCK_TIMEOUT,
    DEADLOCK
  };

  Result result;
  uint8_t nBlobs; // number of blobs replied

  struct BlobData {
    segment_id segment;
    cluster_id cluster;
    blob_id blob;
    blob_size blobSize;
    commit_id commitId;

    /** Used to read the blob's data when processing the message on the client
     */
    std::string_view Content() const;
  };

  /** This class is used to efficiently write the blob data into this message
   */ 
  class Iterator {
    public:
      Iterator(void* payloadPos);
      void SetBlob(segment_id segment, cluster_id cluster, blob_id blob, commit_id commitId, void* data, blob_size size);
      void operator++(); // only increment AFTER setting the blob so the iterator knows how far to increment

      // Read acccess to the blob data header
      BlobData& operator*() const;
    private:
      void* pos;
  };


  /** Creates a new BlobsReadResponse message allocating enough memory to transmit the specified number of 
   *  blobs with the specified total blob size
   */
  static MessagePointer_T<BlobsReadResponse> Create(size_t totalBlobsSize, uint8_t nBlobs = 1);

  /** Creates an empty error response with only the Result value set.
   */
  static MessagePointer_T<BlobsReadResponse> CreateError(Result result);


  static constexpr Type type = Type::BlobsReadResponse;
private:
  BlobsReadResponse(message_size messageSize, uint8_t nBlobs); 
  BlobsReadResponse(Result result);
};


}}}
