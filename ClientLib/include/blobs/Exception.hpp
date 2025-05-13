#pragma once

#include "Config.hpp"

#include <string_view>

namespace blobs {

enum class ExceptionCode {
  Generic,          // Generic error
  DbCloseDuringTxn, // Attempt to close a databse while a transaction is active
  LockTimeout,      // Lock timeout while waiting for attempting to read/write lock a blob
  Deadlock,         // Deadlock while attempting to read/write a blob
  BlobTooLarge,     // Attempted to write a blob, which is too large to write into the database
  BlobDeleted,      // Attempted to read/write a blob, which has been deleted in the current transaction
  BlobLimitReached, // Cannot create more blobs in the cluster
};


class Exception {
public:
  Exception(std::string_view reason, ExceptionCode code = ExceptionCode::Generic);
  BLOBS_EXPORT ~Exception();
  BLOBS_EXPORT const char* what() const;

  /** The exception code, which similar to the exception class may be used to programmatically distinguish the different exceptions
   */
  const ExceptionCode code;
private:
  std::string reason;
};


/** All specific exception types are defined in their own namespace to avoid clashes
 */
namespace exception {

class DbCloseDuringTxn : public Exception {
public:
  DbCloseDuringTxn(const std::string& dbName);
};


class LockTimeout : public Exception {
public:
  LockTimeout();
};

class Deadlock : public Exception {
public:
  Deadlock();
};

class BlobTooLarge : public Exception {
public:
  BlobTooLarge(size_t blobSize);
};


class BlobDeleted : public Exception {
public:
  BlobDeleted();
};


class BlobLimitReached : public Exception {
public:
  BlobLimitReached(segment_id segment, cluster_id cluster);
};


}

}