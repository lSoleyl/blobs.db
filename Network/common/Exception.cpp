// We must mark blobs::Exception for dllexport for it to be exported in the ClientLib
#define BLOBS_EXPORT __declspec(dllexport)

#include <common/Exception.hpp>

#include <string>

namespace blobs {

Exception::Exception(std::string_view reason, ExceptionCode code) : reason(reason), code(code) {}
Exception::~Exception() {}


const char* Exception::what() const {
  return reason.c_str();
}



namespace exception {

DbAlreadyOpen::DbAlreadyOpen(const std::string& dbName) :
  Exception("Attempted to open database: '" + dbName + "' a second time before closing it first", ExceptionCode::DbAlreadyOpen) {}

DbCloseDuringTxn::DbCloseDuringTxn(const std::string& dbName) : 
  Exception("Attempted to close database: '" + dbName + "' while a transaction is still in progress", ExceptionCode::DbCloseDuringTxn) {}

DbNotOpen::DbNotOpen(const std::string& dbName) :
  Exception("Attempted to close database: '" + dbName + "', but it was not opened or has already been closed", ExceptionCode::DbNotOpen) {}

LockTimeout::LockTimeout() : Exception("Waiting for a lock timed out", ExceptionCode::LockTimeout) {}

Deadlock::Deadlock(std::string_view details) : Exception(details, ExceptionCode::Deadlock) {}


BlobTooLarge::BlobTooLarge(size_t blobSize) : Exception(
  "Attempted to write blob of " + std::to_string(blobSize) + " bytes, "
  "which exceeds the maximum allowed blob size of " + std::to_string(blobs::constants::MaxBlobSize) + " bytes!",
  ExceptionCode::BlobTooLarge) {}


BlobDoesNotExist::BlobDoesNotExist() : Exception("Attempt to read/write a blob, which does not exist", ExceptionCode::BlobDoesNotExist) {}

BlobDeleted::BlobDeleted() : Exception("Attempt to read/write a blob, which has been deleted in the current transaction", ExceptionCode::BlobDeleted) {}

ClusterDoesNotExist::ClusterDoesNotExist() : Exception("Attempt to delete a cluster, which does not exist", ExceptionCode::ClusterDoesNotExist) {}

ClusterDeleted::ClusterDeleted() : Exception("Attempt to access a blob in an already deleted cluster", ExceptionCode::ClusterDeleted) {}

SegmentDoesNotExist::SegmentDoesNotExist() : Exception("Attempt to delete a segment, which does not exist", ExceptionCode::SegmentDoesNotExist) {}

SegmentDeleted::SegmentDeleted() : Exception("Attempt to access a cluster in an already deleted segment", ExceptionCode::SegmentDeleted) {}



BlobLimitReached::BlobLimitReached(segment_id segment, cluster_id cluster) : Exception(
  "Cannot create any more blobs in segment " + std::to_string(segment) +  ", cluster " + std::to_string(cluster) + ". Blob limit reached.",
  ExceptionCode::BlobLimitReached
) {}

ClusterLimitReached::ClusterLimitReached(segment_id segment) : Exception(
  "Cannot create any more clusters in segment " + std::to_string(segment) + ". Cluster limit reached.",
  ExceptionCode::ClusterLimitReached
) {}

SegmentLimitReached::SegmentLimitReached() : Exception("Cannot create any more segments in this database. Segment limit reached.", ExceptionCode::SegmentLimitReached) {}


TransactionAlreadyOpen::TransactionAlreadyOpen() : Exception("Cannot start a new transaction, a transaction is already in progress.", ExceptionCode::TransactionAlreadyOpen) {}

InternalCommitError::InternalCommitError(std::string_view reason) : Exception(reason, ExceptionCode::InternalCommitError) {}


}}