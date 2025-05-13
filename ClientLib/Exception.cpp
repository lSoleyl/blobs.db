#include "pch.hpp"
#include <blobs/Exception.hpp>

namespace blobs {

Exception::Exception(std::string_view reason, ExceptionCode code) : reason(reason), code(code) {}
Exception::~Exception() {}


const char* Exception::what() const {
  return reason.c_str();
}



namespace exception {

DbCloseDuringTxn::DbCloseDuringTxn(const std::string& dbName) : 
  Exception("Attempted to close database: '" + dbName + "' while a transaction is still in progress", ExceptionCode::DbCloseDuringTxn) {}


LockTimeout::LockTimeout() : Exception("Waiting for a lock timed out", ExceptionCode::LockTimeout) {}

Deadlock::Deadlock() : Exception("Deadlock situation occurred while attemting to lock a blob", ExceptionCode::Deadlock) {}


BlobTooLarge::BlobTooLarge(size_t blobSize) : Exception(
  "Attempted to write blob of " + std::to_string(blobSize) + " bytes, "
  "which exceeds the maximum allowed blob size of " + std::to_string(blobs::constants::MaxBlobSize) + " bytes!",
  ExceptionCode::BlobTooLarge) {}



BlobDeleted::BlobDeleted() : Exception("Attempted to read/write a blob, which has been deleted in the current transaction", ExceptionCode::BlobDeleted) {}


BlobLimitReached::BlobLimitReached(segment_id segment, cluster_id cluster) : Exception(
  "Cannot create any more blobs in segment " + std::to_string(segment) + 
  ", cluster " + std::to_string(cluster) + ". Blob limit reached."
) {}











}}