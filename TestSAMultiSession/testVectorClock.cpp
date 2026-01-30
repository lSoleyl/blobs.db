#include "pch.hpp"

using namespace blobs;

TEST_CASE("Vector clock with 5 clients") {
  // We want 5 clients
  const int clients = 5;
  constexpr int increments = 25; // perform 25 clock increments per client to get from 'A' to 'Z'

  // Later we will read the content of the vector clock in the main session to compare it with the expected result
  // we open the databse already here to prevent the server from discarding the memory database after the last client disconnects.
  database_ptr db(Database::Open("localhost", "mem:testVectorClock"));


  // Collect timestamps of successful writes here
  std::vector<std::vector<std::chrono::high_resolution_clock::time_point>> timestamps(clients);

  parallel::sync_point syncPoint(clients);
  parallel::for_each(timestamps, [&](auto& timestamps) {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, "localhost", "mem:testVectorClock"));
    // Initialize the clock for this client
    // We must always read with a write lock, otherwise we will get a deadlock
    auto content = db->ReadString(0, 0, 0, blobs::Lock::Write);
    auto index = content.length();
    REQUIRE_MESSAGE(index < clients, "Clock length cannot exceed number of clients!");
    content.push_back('A'); // 0x41
    db->WriteString(0, 0, 0, content);
    Transaction::Commit(session);

    syncPoint.wait(); // Synchronize all clients here

    // Now increment the vectorclock at this client's index specified number of times (we should end up at 'z')
    for (int i = 0; i < increments; ++i) {
      auto content = db->ReadString(0, 0, 0, blobs::Lock::Write); // <- write lock to prevent deadlocks with other clients
      timestamps.push_back(std::chrono::high_resolution_clock::now()); // note down the time, when we get the write lock
      ++content[index];
      // Write back the modified clock and commit
      db->WriteString(0, 0, 0, content);
      Transaction::Commit(session);
    }
  });

  // First just check that each client performed the specified number of increments
  for (int i = 0; i < clients; ++i) {
    CAPTURE(i);
    REQUIRE(timestamps[i].size() == increments);
  }


  // Now read the vector clock value after all clients finished in the main session and compare it to the expected value
  auto clockContent = db->ReadString(0, 0, 0);
  auto expectedClock = std::string(clients, 'A' + increments);

  REQUIRE_MESSAGE(clockContent == expectedClock, "Vector clock content wrong after all clients finished");

  // The last check we have to perform is to ensure that no client was starved out.
  auto latestFirstTimestamp = std::max_element(timestamps.begin(), timestamps.end(), [](auto& a, auto& b) { return a[0] < b[0]; });
  auto earliestLastTimestamp = std::min_element(timestamps.begin(), timestamps.end(), [](auto& a, auto& b) { return a[increments-1] < b[increments-1]; });

  // This should only fail if one client was completely starved and started after the first one already finished.
  // This would imply bad scheduling by the server as the server should schedule using round robin.
  // Or it could imply bad thread scheduling by the OS... If the thread simply does not run until the other clients complete then
  // the server can do little about it and this seems to happen from time to time in the tests...
  REQUIRE(latestFirstTimestamp < earliestLastTimestamp);
}
