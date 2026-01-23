#pragma once

#ifndef BLOBS_EXPORT
#define BLOBS_EXPORT __declspec(dllimport)
#endif

#include <cstdint>
#include <limits>

// Define all predefined type choices made to be able to easily recompile with other sizes if necessary
namespace blobs {

  using message_size = uint32_t; // limits the maximum size of a single network::Message (and indirectly the maximum size of a single blob)

  using client_id = uint16_t;   // limits the number of concurrent server connections
  using database_id = uint8_t;  // limits the number of concurrently opened databases for one client (per server)
  using segment_id = uint32_t;  // limits the number of segments in the database
  using cluster_id = uint32_t;  // limits the number of clusters in a segment
  using blob_id = uint32_t;     // limits the number of blobs in a cluster
  using blob_size = uint32_t;   // limits the size of a blob (also limited by message_size)
  using commit_id = uint64_t;   // limits the number of commits in the databse before the server needs to reboot to reorganize all commit ids

  using connection_id = uint16_t; // limits the number distinct server connections a single client can establish

  // Global constants derived from the above type definitions
  namespace constants {
    /** Helper constant, which should always be larger or equal to the size of the BlobsReadResponse and TransactionCommit messages to ensure we can define a 
     *  safe blob size limit to be able to transmit at least one blob per message.
     */
    constexpr message_size _BlobMessageSize = 0x100;

    /** The maximum allowed blob size is chosen in a way to ensure that neither BlobsReadResponse or TransactionCommit need to be split for transmitting a single blob.
     */
    constexpr blob_size MaxBlobSize = std::is_same_v<blob_size, message_size> ? std::numeric_limits<blob_size>::max() - _BlobMessageSize :
                                   (sizeof(blob_size) < sizeof(message_size)) ? std::numeric_limits<blob_size>::max() - 1 :
                                                                                std::numeric_limits<message_size>::max() - _BlobMessageSize;

    /** Special blob size value, which will be specified in a TransactionCommit message to delete a blob.
     */
    constexpr blob_size DeleteBlobSize = MaxBlobSize + 1;


    /** Highest valid blob id to use for storing blobs. All others have special semantics or are reserved for future use
     */
    constexpr blob_id MaxBlobId = std::numeric_limits<blob_id>::max() - 0x100;
    
    /** Special blob id used to acquire a write lock for creating new blobs in the cluster and reading the next free blob id
     */
    constexpr blob_id NextFreeBlobId = MaxBlobId + 1;

    /** Special blob id, which needs to be write locked by clients who want to delete a cluster. Committing this blob will result in deletion of the cluster.
     *  If a write lock to this blob id is held, no other blobs in the cluster can be locked by any other client.
     */
    constexpr blob_id ClusterDeleteId = MaxBlobId + 2;

    /** Special blob id, which will may be read to retrieve the list of all existing blobs in a cluster.
     */
    constexpr blob_id BlobListId = MaxBlobId + 3;
    
    /** Highest valid cluster id to use for storing clusters. All others have special semantics or are reserved for future use
     */
    constexpr cluster_id MaxClusterId = std::numeric_limits<cluster_id>::max() - 0x100;

    /** Special cluster id used to acquire a write lock for creating new clusters in the segment and reading the next free cluster id
     */
    constexpr cluster_id NextFreeClusterId = MaxClusterId + 1;

    /** Special cluster id, which needs to be write locked by clients who want to delete the segment. Committing (segment, DeleteSegmentId, DeleteClusterId) will 
     *  delete the whole segment. If a write lock to this blob id is held, no other blobs in the segment can be locked by any other client.
     */ 
    constexpr cluster_id SegmentDeleteId = MaxClusterId + 2;

    /** Special cluster id that is used as (segment, ClusterListId, BlobListId) to read the list of all clusters in the specified segment.
     */
    constexpr cluster_id ClusterListId = MaxClusterId + 3;


    /** Highest valid segment id to use for storing segments. All others have special semantics or are reserved for future use
     */
    constexpr segment_id MaxSegmentId = std::numeric_limits<segment_id>::max() - 0x100;

    /** Special segment id used to acquire a write lock for creating new segments in the database and reading the next free segment id
     */
    constexpr segment_id NextFreeSegmentId = MaxSegmentId + 1;

    /** Sepcial segment id that is used as (SegmentListId, ClusterListId, BlobListId) to read the list of all segments in the database,.
     */
    constexpr segment_id SegmentListId = MaxSegmentId + 1;
  }
}

#define _STR(x) #x
#define STR(x) _STR(x)

#define MSVS_WARN(x) __pragma(message(__FILE__ "(" STR(__LINE__) "): warning: " x))

#define TODO(x) MSVS_WARN("'" __FUNCTION__ "' TODO: " x)
#define FIXME(x) MSVS_WARN("'" __FUNCTION__ "' FIXME: " x)

