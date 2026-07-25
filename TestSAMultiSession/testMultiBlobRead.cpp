#include "pch.hpp"

using namespace blobs;
using namespace std::chrono_literals;


// Here one client will write multiple blobs and another client will read them
TEST_CASE("Test basic write/read multiple blobs") {
  auto connStr = "localhost/mem:testBasicMultiBlobRead";

  // Prepare the database
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->CreateString(0, 0, "");
  db->CreateString(0, 0, "");
  db->CreateString(0, 0, "");
  db->CreateString(0, 0, "");
  Transaction::Commit(session);


  parallel::sync_point writeCompleted(2); // use a sync point to avoid reading in the second client before the first finished writing
  parallel::run({
    // The first client will write the blobs 0-4
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));

      auto requestPtr = MultiBlobRequest::Create(5);
      auto& request = *requestPtr;
      request[0].Set(0, 0, 0).SetString("000");
      request[1].Set(0, 0, 1).SetString("001");
      request[2].Set(0, 0, 2).SetString("002");
      request[3].Set(0, 0, 3).SetString("003");
      request[4].Set(0, 0, 4).SetString("004");
      
      db->WriteBlobs(request);
      Transaction::Commit(session);
      writeCompleted.wait();
    },

    // The second client will wait for the first client to complete its write operation and then 
    // read all written blobs and check the returned values.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      writeCompleted.wait();
      
      auto requestPtr = MultiBlobRequest::Create(5);
      auto& request = *requestPtr;
      request[0].Set(0, 0, 0);
      request[1].Set(0, 0, 1);
      request[2].Set(0, 0, 2);
      request[3].Set(0, 0, 3);
      request[4].Set(0, 0, 4);

      // Now read all 5 blobs in one request
      db->ReadBlobs(request);

      // Validate the returned blobs
      CHECK(request[0].GetStringView() == "000");
      CHECK(request[1].GetStringView() == "001");
      CHECK(request[2].GetStringView() == "002");
      CHECK(request[3].GetStringView() == "003");
      CHECK(request[4].GetStringView() == "004");
    }
  });
}

// This test makes sure that a multi-blob read atomically acquires all locks at once and not one after another,
// which would otherwise result in deadlocks.
TEST_CASE("Test atomic multi-read") {
  auto connStr = "localhost/mem:testAtomicMultiRead";

  // Prepare the database
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->WriteString(0, 0, 0, "000");
  db->CreateString(0, 0, "001");
  Transaction::Commit(session);

  parallel::sync_point firstBlobLocked(2);
  parallel::run({
    // The first client will acquire a write lock on 000
    // and then attempt to write 001
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->WriteString(0, 0, 0, "000-2");
      firstBlobLocked.wait();
      std::this_thread::sleep_for(50ms); // long enough to ensure that client 2 had a chance to have its read scheduled on the server
      db->WriteString(0, 0, 1, "001-2");
      Transaction::Commit(session);
    },

    // The second client will wait for the first client to acquire its write lock and then
    // attempt to read both blobs.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));

      auto requestPtr = MultiBlobRequest::Create(2);
      auto& request = *requestPtr;

      request[0].Set(0, 0, 0);
      request[1].Set(0, 0, 1);
      firstBlobLocked.wait();

      // Now try to read 2 blobs (one of which is currently locked by the first client)
      db->ReadBlobs(request);

      // This read must have been delayed until the first client released its locks, i.e. committed its transaction -> verify it
      CHECK(request[0].GetStringView() == "000-2");
      CHECK(request[1].GetStringView() == "001-2");
    }
  });
}



// TODO: Test with partial reads (blobs partially in transaction commit cache/database cache)
// TODO: Test with requesting a not existing blob in the requested blob range
// TODO: Test with dirty reads
// TODO: Test in MVCC mode (also test dirty reads here)
// TODO: Test that reading more than 2GB succeeds by splitting up the response into multiple messages


