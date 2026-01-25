#pragma once

#include <doctest/doctest.h>
#include <blobs/Blobs.hpp>

#include "parallel.hpp"
#include <chrono>
#include <iostream>
#include <vector>


struct CloseUponDelete {
  void operator()(blobs::Database* db) { 
    // Abort any currently running transaction before attempting to close the database to avoid throwing an exception in Close()
    blobs::Transaction::Abort(db->GetSession());
    db->Close();
  }
};

/** unique_ptr with custom deleter for blobs::Database, which aborts any currently running transaction and then calls Close() instead of the destructor
 */
using database_ptr = std::unique_ptr<blobs::Database, CloseUponDelete>;


namespace std {
/** Generic vector serializer for display of expected values
 *  We must define it in std namespace for ADL to find the function
 */
template<typename T>
std::ostream& operator<<(std::ostream& out, const std::vector<T>& vec) {
  out << "[";
  if (vec.empty()) {
    return out << "]";
  }
  auto pos = vec.begin();
  out << *pos++;

  for (auto end = vec.end(); pos != end; ++pos) {
    out << ", " << *pos;
  }

  return out << "]";
}


namespace chrono {

/** Serialization for timestamps
 */
inline std::ostream& operator<<(std::ostream& out, const std::chrono::high_resolution_clock::time_point& t) {
  return out << std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count() << "us";
}

}}