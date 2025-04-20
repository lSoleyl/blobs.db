#pragma once

#ifndef BLOBS_EXPORT
#define BLOBS_EXPORT __declspec(dllimport)
#endif

#include <cstdint>

// Define all predefined type choices made to be able to easily recompile with other sizes if necessary
namespace blobs {

using message_size = uint32_t; // limits the maximum size of a single network::Message

using client_id = uint16_t; // limits the number of concurrent server connections
using database_id = uint8_t; // limits the number of concurrently opened databases for one client (per server)
using segment_id = uint32_t; // limits the number of segments in the database
using cluster_id = uint32_t; // limits the number of clusters in a segment
using blob_id = uint32_t; // limits the number of blobs in a cluster
using blob_size = uint32_t; // limits the size of a blob
using commit_id = uint64_t; // limits the number of commits in the databse before the server needs to reboot to reorganize all commit ids

using connection_id = uint16_t; // limits the number distinct server connections a single client can establish


//TODO: the actual max blob size should be a bit smaller than the type allows for to be able that we never have to split a single blob across multiple messages.
//TODO: also the maximum id will be a bit lower as we will reserve special ids to refer to the segment/cluster tables


}

