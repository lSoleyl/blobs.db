#include "pch.hpp"

using namespace blobs;
using namespace std::chrono_literals;

/** Test for correct lock timeout handling when acquiring read locks
 */
TEST_CASE("Test lock timeout read") {
  auto connStr = "localhost/mem:testLockTimeoutRead";  

  parallel::sync_point syncPoint(4);

  std::chrono::high_resolution_clock::time_point c1, c2, c3, c4;


  parallel::run({
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->WriteString(0, 0, 0, "test"); // write lock the string
      syncPoint.wait();
      std::this_thread::sleep_for(50ms); // long enough to detect the lock timeouts
      c1 = std::chrono::high_resolution_clock::now();
    },

    // 2. client reads with an immediate timeout
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(0); // timeout immediately
      syncPoint.wait();
      CHECK_THROWS_AS(db->ReadString(0, 0, 0), exception::LockTimeout);
      c2 = std::chrono::high_resolution_clock::now();
    },

    // 3. client reads with a 10 ms timeout
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(10); // timeout immediately
      syncPoint.wait();
      CHECK_THROWS_AS(db->ReadString(0, 0, 0), exception::LockTimeout);
      c3 = std::chrono::high_resolution_clock::now();
    },


    // 4. client reads with a 100 ms timeout
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(100); // timeout immediately
      syncPoint.wait();
      CHECK_NOTHROW_MESSAGE(db->ReadString(0, 0, 0), "Client 4 should not run into a lock timeout");
      c4 = std::chrono::high_resolution_clock::now();
    },
  });


  REQUIRE_MESSAGE(c2 < c3, "The immediate timeout should trigger first");
  REQUIRE_MESSAGE(c3 < c1, "The 10ms timeout should trigger before the first client finishes waiting");
  REQUIRE_MESSAGE(c1 < c4, "The fourth client should finish last");
}


/** Test for correct lock timeout handling when acquiring write locks
 */
TEST_CASE("Test lock timeout write") {
  auto connStr = "localhost/mem:testLockTimeoutWrite";

  parallel::sync_point syncPoint(4);

  std::chrono::high_resolution_clock::time_point c1, c2, c3, c4;


  parallel::run({
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->WriteString(0, 0, 0, "test"); // write lock the string
      syncPoint.wait();
      std::this_thread::sleep_for(50ms); // long enough to detect the lock timeouts
      c1 = std::chrono::high_resolution_clock::now();
    },

    // 2. client reads with an immediate timeout
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(0); // timeout immediately
      syncPoint.wait();
      CHECK_THROWS_AS(db->WriteString(0, 0, 0, "c2"), exception::LockTimeout);
      c2 = std::chrono::high_resolution_clock::now();
    },

    // 3. client reads with a 10 ms timeout
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(10); // timeout immediately
      syncPoint.wait();
      CHECK_THROWS_AS(db->WriteString(0, 0, 0, "c3"), exception::LockTimeout);
      c3 = std::chrono::high_resolution_clock::now();
    },


    // 4. client reads with a 100 ms timeout
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(100); // timeout immediately
      syncPoint.wait();
      CHECK_NOTHROW_MESSAGE(db->WriteString(0, 0, 0, "c4"), "Client 4 should not run into a lock timeout");
      c4 = std::chrono::high_resolution_clock::now();
    },
    });


  REQUIRE_MESSAGE(c2 < c3, "The immediate timeout should trigger first");
  REQUIRE_MESSAGE(c3 < c1, "The 10ms timeout should trigger before the first client finishes waiting");
  REQUIRE_MESSAGE(c1 < c4, "The fourth client should finish last");
}


/** This test ensures that a lock timeout will NOT abort a client's transaction.
 */
TEST_CASE("Test lock timeout keeps transaction open") {
  auto connStr = "localhost/mem:testLockTimeoutTxnPersistence";
  
  // Prepare database
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->CreateString(0, 0, "");
  Transaction::Commit(session);

  
  parallel::sync_point syncPoint(2);
  parallel::sync_point syncPoint2(2);

  std::chrono::high_resolution_clock::time_point c1, c2;

  parallel::run({
    // The first client will hold a write lock on 0,0,0 and then attempt to read on 0,0,1 without a lock timeout
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->WriteString(0, 0, 0, "c1");
      syncPoint.wait();
      syncPoint2.wait(); // first client tried to read and failed (but the transaction should still run, so the next read will block)
      CHECK(db->ReadString(0, 0, 1) == "c2");
      c1 = std::chrono::high_resolution_clock::now();
    },

    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(0); // This will also avoid the deadlock situation to be detected as such (as long as this client triggers the deadlock)
      db->WriteString(0, 0, 1, "c2");
      syncPoint.wait();
      CHECK_THROWS_AS_MESSAGE(db->ReadString(0, 0, 0), exception::LockTimeout, "First read of second client should time out");
      syncPoint2.wait();
      CHECK_MESSAGE(db->HasTransaction() == true, "The first client should still be inside a transaction after the first timeout");
      std::this_thread::sleep_for(25ms); // Wait long enough for the fist client to perform its read on 0,0,1

      // Now read a second time (this would be considered a deadlock if not for the lock timeout of 0)
      CHECK_THROWS_AS_MESSAGE(db->ReadString(0, 0, 0), exception::LockTimeout, "Second read of second client should time out");
      c2 = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
      // <- now the first client can complete its read
    }
  });

  REQUIRE_MESSAGE(c2 < c1, "Client 2 should finish before client 1 because the transaction should continue after the lock timeout");
}


/** A small test case to test correct working of the TryRead utility method
 */
TEST_CASE("Test TryReadBlob") {
  auto connStr = "localhost/mem:testTryReadBlob";


  parallel::sync_point blobLocked(2), firstReadCompleted(2), lockReleased(2);

  parallel::run({

    // The first client will simply write lock a blob and release the lock later.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->WriteString(0, 0, 0, "test");
      blobLocked.wait();
      firstReadCompleted.wait();
      // Now we release the lock
      Transaction::Commit(session);
      lockReleased.wait();
    },

    // The second client will attempt to read the locked blob via ReadString() and TryReadBlob() and validate that
    // the timeouts are correctly handled and that reading succeeds once the blob is unlocked.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->SetLockTimeout(10); // long enough to measure it
      blobLocked.wait();

      auto t1 = std::chrono::high_resolution_clock::now();
      CHECK_THROWS_AS_MESSAGE(db->ReadString(0, 0, 0), exception::LockTimeout, "A regular read should throw a LockTimeout");
      auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t1).count();
      CHECK_MESSAGE(durationMs >= 10, "The first read should have timed out after at least 10ms");

      // Now test with TryReadBlob (the result should be there immediately)
      t1 = std::chrono::high_resolution_clock::now();
      CHECK_MESSAGE(db->TryReadBlob(0, 0, 0) == false, "TryReadBlob() should fail while the lock is still held");
      durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t1).count();
      CHECK_MESSAGE(durationMs <= 1, "TryReadBlob() should complete in well under the original 10ms timeout");

      // Test that TryReadBlob resets the timeout correctly
      t1 = std::chrono::high_resolution_clock::now();
      CHECK_THROWS_AS_MESSAGE(db->ReadString(0, 0, 0), exception::LockTimeout, "A regular read should still throw a LockTimeout");
      durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - t1).count();
      CHECK_MESSAGE(durationMs >= 10, "The second read should still have timed out after at least 10ms");
      
      // Wait for the first client to release its lock
      firstReadCompleted.wait();
      lockReleased.wait();

      // Now try read should succeed in reading the blob
      CHECK_MESSAGE(db->TryReadBlob(0, 0, 0) == true, "TryReadBlob() should succeed after the lock is released");
      CHECK_MESSAGE(db->TryReadBlob(0, 0, 0) == true, "TryReadBlob() should still succeed when called a second time");
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "test", "ReadString() should succeed after TryReadBlob()");
    }
  });



}

