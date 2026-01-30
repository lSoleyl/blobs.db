#include "pch.hpp"

using namespace blobs;

// A simple test case where the blob consists of a single integer that will be incremented by each 
// client. This ensure that locking works correctly and no client sees an outdated value.
TEST_CASE("Simple write conflict with 2 clients") {
  // Prepare the databse by writing a 0 into the first blob
  database_ptr db(Database::Open("localhost", "mem:testSimpleWriteConflict"));
  int number = 0;
  db->WriteBlob(0, 0, 0, &number, sizeof(number));
  Transaction::Commit();

  std::vector<std::vector<std::chrono::high_resolution_clock::time_point>> timestamps(2);
  parallel::sync_point syncPoint(2);

  parallel::for_each(timestamps, [&](auto& timestamps) {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, "localhost", "mem:testSimpleWriteConflict"));
    syncPoint.wait(); // let both clients run at the same time

    // Perform 10 increments in each client
    for (int i = 0; i < 10; ++i) {
      int number = *static_cast<const int*>(db->ReadBlob(0, 0, 0, blobs::Lock::Write).first); // <- write lock to prevent deadlocks
      timestamps.push_back(std::chrono::high_resolution_clock::now());
      ++number;
      db->WriteBlob(0, 0, 0, &number, sizeof(number));
      Transaction::Commit(session);
    }
  });

  // Now read the current content in the main session
  number = *static_cast<const int*>(db->ReadBlob(0, 0, 0).first);
  REQUIRE(number == 20); // We performed 20 increments across the two clients

  // Now test that no client was starved (i.e. the first timestamp of a client should no be after the last timestamp the other client)
  // This would imply bad scheduling by the server, which should hand out locks in a round robin fashion.
  auto latestFirstTimestamp = std::max(timestamps[0][0], timestamps[1][0]);
  auto earliestLastTimestamp = std::min(timestamps[0][9], timestamps[1][9]);
  REQUIRE_MESSAGE(latestFirstTimestamp < earliestLastTimestamp, "A client was starved");
}