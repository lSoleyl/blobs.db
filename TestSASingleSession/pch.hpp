#pragma once

#include <doctest/doctest.h>
#include <blobs/Blobs.hpp>



struct CloseUponDelete {
  void operator()(blobs::Database* db) { db->Close(); }
};

/** unique_ptr with custom deleter for blobs::Database, which calls Close() instead of the destructor
 */
using database_ptr = std::unique_ptr<blobs::Database, CloseUponDelete>;