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
