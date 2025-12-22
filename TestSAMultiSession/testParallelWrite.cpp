#include "pch.hpp"

using namespace blobs;


// Here we will perform something similar like a vector clock, but instead of writing the clock into one blob
// each client will write into its own blobs thus having no lock contention, which means that we can read the blob's previous
// state using a read lock without deadlocking with another client
TEST_CASE("Test parallel write with 5 clients") {
  const int clients = 5;

  // Open the database now to prevent the server from cleaning up the in-memory database when the last client disconnects. 
  // (Maybe we could control that with a flag?)
  database_ptr db(Database::Open("localhost", "mem:testParallelWrite"));

  parallel::sync_point syncPoint(clients);
  std::vector<int> clientIds(clients); // no timepoints here, as there is nothing I can check here
  std::iota(clientIds.begin(), clientIds.end(), 0);

  parallel::for_each(clientIds, [&](int clientId) {
    

    auto session = Session::Create();
    database_ptr db(Database::Open(session, "localhost", "mem:testParallelWrite"));
    int number = 0;
    auto blobId = db->CreateBlob(0, 0, &number, sizeof(number));
    Transaction::Commit(session);
    
    syncPoint.wait(); // <- sync all threads here

    for (int i = 0; i < 10; ++i) {
      int number = *static_cast<const int*>(db->ReadBlob(0, 0, blobId).first); // since we expect no conflicts, we can read lock the blob for reading
      CAPTURE(clientId);
      CAPTURE(blobId);
      REQUIRE(number == i); // <- simply check that there are no lost or additional updates
      ++number;
      db->WriteBlob(0, 0, blobId, &number, sizeof(number));
      Transaction::Commit(session);
    }
  });


  // Now validate in the main thread that we have 5 blobs with the number 10 in it
  for (int clientId = 0; clientId < clients; ++clientId) {
    CAPTURE(clientId);
    int number;
    REQUIRE_NOTHROW(number = *static_cast<const int*>(db->ReadBlob(0, 0, clientId + 1).first));
    REQUIRE(number == 10);
  }

  // Since the clients run completely in parallel, we cannot make any assertions about the timstamps of this process
}