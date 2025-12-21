#pragma once

#include <doctest/doctest.h>
#include <blobs/Blobs.hpp>

#include "parallel.hpp"
#include <chrono>


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

