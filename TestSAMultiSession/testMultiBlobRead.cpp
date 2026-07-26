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


// This test will check correct handling of multi blob reads if the blobs are partially in commit cache and partially in database cache
TEST_CASE("Test partial multi blob read") {
  auto connStr = "localhost/mem:testPartialMultiRead";

  // Prepare the database
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->WriteString(0, 0, 0, "000");
  db->CreateString(0, 0, "001");
  db->CreateString(0, 0, "002");
  Transaction::Commit(session);


  // For this test we just need one client
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr));
    REQUIRE(db->ReadString(0, 0, 0) == "000"); // put into database cache
    db->WriteString(0, 0, 1, "001-2"); // put into commit cache

    // Now read all three blobs
    auto requestPtr = MultiBlobRequest::Create(3);
    auto& request = *requestPtr;
    request.SetBlobRange(0, 0);
    db->ReadBlobs(request);

    REQUIRE(request[0].GetStringView() == "000"); // this blob is read from database cache
    REQUIRE(request[1].GetStringView() == "001-2"); // the client should see the not yet committed write
    REQUIRE(request[2].GetStringView() == "002"); // this blob is read from the database server
  }
}

TEST_CASE("Test multiread with not existing blobs") {
  auto connStr = "localhost/mem:testMultiReadWithNonExisting";

  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  // Now attempt to read the first 3 blobs (only the first one exists)
  auto requestPtr = MultiBlobRequest::Create(3);
  requestPtr->SetBlobRange(0, 0);
  REQUIRE_THROWS_AS_MESSAGE(db->ReadBlobs(*requestPtr), exception::BlobDoesNotExist, "Reading non existent blobs should throw");


  // Now attempt to write the non existent blobs
  auto& request = *requestPtr;
  request[0].SetString("000");
  request[1].SetString("001");
  request[2].SetString("002");
  REQUIRE_THROWS_AS_MESSAGE(db->WriteBlobs(request), exception::BlobDoesNotExist, "Writing to non existent blobs should throw");

  Transaction::Commit(session);

  // Now validate that reading these blobs will return the unmodified database state
  REQUIRE(db->ReadString(0, 0, 0) == ""); // The "000" wasn't committed, because it was part of the failed write request
  REQUIRE_THROWS_AS(db->ReadString(0, 0, 1), exception::BlobDoesNotExist);
  REQUIRE_THROWS_AS(db->ReadString(0, 0, 2), exception::BlobDoesNotExist);
}

TEST_CASE("Test multiread with deleted blobs") {
  auto connStr = "localhost/mem:testMultiReadWithDeleted";
  
  // Prepare db
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->WriteString(0, 0, 0, "000");
  db->CreateString(0, 0, "001");
  db->CreateString(0, 0, "002");
  Transaction::Commit(session);

  auto requestPtr = MultiBlobRequest::Create(3);
  auto& request = *requestPtr;
  request.SetBlobRange(0, 0);


  // First perform a successful read of these 3 blobs
  REQUIRE_NOTHROW_MESSAGE(db->ReadBlobs(request), "Multi blob read should not fail before deleting the blob");
  REQUIRE(request[0].GetStringView() == "000");
  REQUIRE(request[1].GetStringView() == "001");
  REQUIRE(request[2].GetStringView() == "002");


  // Now delete a blob
  db->DeleteBlob(0, 0, 1);
  REQUIRE_THROWS_AS_MESSAGE(db->ReadBlobs(request), exception::BlobDeleted, "Multi blob read should fail after deleting the blob");
}

TEST_CASE("Test multi blob dirty read") {
  auto connStr = "localhost/mem:testMultiDirtyRead";

  // Prepare db
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->WriteString(0, 0, 0, "000");
  db->CreateString(0, 0, "001");
  db->CreateString(0, 0, "002");
  Transaction::Commit(session);

  parallel::sync_point ready(2);
  parallel::run({
    // The first client will simply write lock all three blobs by updating them
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      auto requestPtr = MultiBlobRequest::Create(3);
      auto& request = *requestPtr;
      request[0].Set(0, 0, 0).SetString("000-x");
      request[1].Set(0, 0, 1).SetString("001-x");
      request[2].Set(0, 0, 2).SetString("002-x");
      db->WriteBlobs(request);
      ready.wait();
      std::this_thread::sleep_for(50ms); // just long enough to let the dirty read finish before the commit
      Transaction::Commit(session);
    },

    // The second client will wait for the first client to perform its write operation and will then trigger a dirty read
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      auto requestPtr = MultiBlobRequest::Create(3);
      auto& request = *requestPtr;
      request.SetBlobRange(0, 0);
      ready.wait();
      db->ReadBlobs(request, Lock::None);
      // Now we should have read the old state
      CHECK(request[0].GetStringView() == "000");
      CHECK(request[1].GetStringView() == "001");
      CHECK(request[2].GetStringView() == "002");
    }
  });

  // Finally validate in the outer session that the changes have been committed
  auto requestPtr = MultiBlobRequest::Create(3);
  auto& request = *requestPtr;
  request.SetBlobRange(0, 0);
  db->ReadBlobs(request);

  REQUIRE(request[0].GetStringView() == "000-x");
  REQUIRE(request[1].GetStringView() == "001-x");
  REQUIRE(request[2].GetStringView() == "002-x");

}

TEST_CASE("Test multi read MVCC") {
  auto connStr = "localhost/mem:testMultiReadMVCC";

  // Prepare db
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->WriteString(0, 0, 0, "000");
  db->CreateString(0, 0, "001");
  db->CreateString(0, 0, "002");
  Transaction::Commit(session);
  
  parallel::sync_point ready(2), commit(2);
  parallel::run({
    // The first client will update all blobs and wait for the second client to be ready before committing
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      auto requestPtr = MultiBlobRequest::Create(3);
      auto& request = *requestPtr;
      request[0].Set(0, 0, 0).SetString("000-x");
      request[1].Set(0, 0, 1).SetString("001-x");
      request[2].Set(0, 0, 2).SetString("002-x");
      db->WriteBlobs(request);
      ready.wait();
      Transaction::Commit(session);
      commit.wait();
    },

    // The second client will open the database in MVCC mode and perform a multi blob read and then a dirty multi blob read after the first client commits
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));
      auto requestPtr = MultiBlobRequest::Create(3);
      auto& request = *requestPtr;
      request.SetBlobRange(0, 0);
      db->ReadBlobs(request);
      CHECK(request[0].GetStringView() == "000");
      CHECK(request[1].GetStringView() == "001");
      CHECK(request[2].GetStringView() == "002");
      // Now wait for the first client to commit its writes
      ready.wait(); 
      commit.wait();

      // Re-reading the same blobs should still return the same values
      db->ReadBlobs(request);
      CHECK(request[0].GetStringView() == "000");
      CHECK(request[1].GetStringView() == "001");
      CHECK(request[2].GetStringView() == "002");

      // Peforming a dirty read should return the updated values
      db->ReadBlobs(request, Lock::None);
      CHECK(request[0].GetStringView() == "000-x");
      CHECK(request[1].GetStringView() == "001-x");
      CHECK(request[2].GetStringView() == "002-x");
    }
  });
}


TEST_CASE("Multi blob read 5GB") {
  auto connStr = "localhost/mem:testMultiRead5GB";

  // Prepare db (we should create 50 blobs of 100MB each to have a 5GB database) - this will be very memory intensive
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  size_t hMB = 100 * 1024 * 1024;

  // Use a temporary session to pump up the database to avoid keeping the 5GB around in the client's cache
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr));
    std::string hundredMB(hMB, 'A');
    for (int i = 0; i < 50; ++i) {
      db->CreateString(0, 0, hundredMB);
      Transaction::Commit(session); // commit blob by blob to avoid filling the client's commit cache (it will still be put into the database cache)
    }
  } // session and client cache will now be released

  
  // Read these blobs in a separate session to avoid reading from cache and force the server to send the blobs
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr));
    auto requestPtr = MultiBlobRequest::Create(50);
    auto& request = *requestPtr;
    request.SetBlobRange(0, 0, 1); // skip blob 0
    db->ReadBlobs(request);

    // Now all 50 100MB blobs should be in the client
    for (int i = 0; i < 50; ++i) {
      CAPTURE(i);
      REQUIRE(request[i].size == hMB);
      REQUIRE(request[i].GetStringView().front() == 'A');
      REQUIRE(request[i].GetStringView().back() == 'A');
    }
  }
}

