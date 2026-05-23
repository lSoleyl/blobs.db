#include "pch.hpp"

using namespace blobs;


// Test whether sticky locks are held across transactions if enabled
TEST_CASE("Synchronization with sticky locks enabled and held across transactions") {
  // Create own session for this to avoid impacting other tests in case something goes wrong here
  const char* dbName = "mem:testSyncWithStickyLocks";
  database_ptr db(Database::Open("localhost", dbName));
  db->CreateString(0, 0, "test"); // create blob 1
  Transaction::Commit();

  parallel::sync_point syncPoint(2);

  std::atomic<std::chrono::high_resolution_clock::time_point> firstTransactionEnded, secondReadCompleted;

  parallel::run({
    // The first client will acquire a write lock in the first transaction, commit it and then start a new transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      db->WriteString(0, 0, 0, "updated");
      Transaction::Commit(session);
      
      // Now start the second transaction by reading another blob (this client will still hold the write lock on 0,0,0 from the previous transaction)
      db->ReadBlob(0, 0, 1);
      syncPoint.wait(); // sync with other thread
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); // just long enough to be sure the other thread is blocked
      firstTransactionEnded = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will just open the database, wait for the first client to finish preparations and then attempt to acquire the write lock,
    // which should block for as long as the other client holds the transaction open.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();

      // Try to read the first blob (which will block as the other client still holds a stick lock)
      REQUIRE(db->ReadString(0, 0, 0) == "updated");
      secondReadCompleted = std::chrono::high_resolution_clock::now();
    }
  });

  
  REQUIRE_MESSAGE(firstTransactionEnded.load() < secondReadCompleted.load(), "First client's sticky locks should have blocked the second client");
}



// Test whether sticky locks are revoked between transactions if enabled
TEST_CASE("Synchronization with sticky locks enabled and revoked between transactions") {
  // Create own session for this to avoid impacting other tests in case something goes wrong here
  const char* dbName = "mem:testSyncWithStickyLocksRevoked";
  database_ptr db(Database::Open("localhost", dbName));
  db->CreateString(0, 0, "test"); // create blob 1
  Transaction::Commit();

  parallel::sync_point syncPoint(2);
  parallel::sync_point syncPoint2(2);

  std::atomic<std::chrono::high_resolution_clock::time_point> firstReadCompleted, firstWriteCompleted, secondReadCompleted, secondTransactionCompleted;

  parallel::run({
    // The first client will acquire a write lock in the first transaction, commit it and then start a new transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      db->WriteString(0, 0, 0, "updated");
      Transaction::Commit(session);
      syncPoint.wait(); // sync with other thread
      std::this_thread::sleep_for(std::chrono::milliseconds(10)); // just long enough to be sure the other thread will read first

      // Now start the second transaction by reading another blob (the write lock will have been revoked)
      db->ReadBlob(0, 0, 1);
      firstReadCompleted = std::chrono::high_resolution_clock::now();
      db->WriteString(0, 0, 0, "updated2"); // try to write lock the first blob (this should block because the second client holds a read lock)
      firstWriteCompleted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will just open the database, wait for the first client to finish preparations and then attempt to acquire the write lock,
    // which should block for as long as the other client holds the transaction open.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();

      // Try to read the first blob (which will block as the other client still holds a stick lock)
      REQUIRE(db->ReadString(0, 0, 0) == "updated");
      secondReadCompleted = std::chrono::high_resolution_clock::now();
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); // just long enough to be sure the other thread will be blocked while waiting for its write lock
      secondTransactionCompleted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    }
  });


  REQUIRE_MESSAGE(secondReadCompleted.load() < firstReadCompleted.load(), "First client's sticky locks should have been revoked by the second client");
  REQUIRE_MESSAGE(firstReadCompleted.load() < secondTransactionCompleted.load(), "First client's read should complete before the second client's transaction");
  REQUIRE_MESSAGE(secondTransactionCompleted.load() < firstWriteCompleted.load(), "First client's write should complete after the second client's transaction");
}


// Now test that disabling sticky locks will result in no locks held across transactions
TEST_CASE("Synchronization with sticky locks disabled") {
  // Create own session for this to avoid impacting other tests in case something goes wrong here
  const char* dbName = "mem:testSyncWithoutStickyLocks";
  database_ptr db(Database::Open("localhost", dbName));
  db->CreateString(0, 0, "test"); // create blob 1
  Transaction::Commit();

  parallel::sync_point syncPoint(2);

  std::atomic<std::chrono::high_resolution_clock::time_point> firstTransactionEnded, secondReadCompleted;

  parallel::run({
    // The first client will acquire a write lock in the first transaction, commit it and then start a new transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      db->WriteString(0, 0, 0, "updated");
      Transaction::Commit(session);
      CHECK_MESSAGE(Transaction::UseStickyLocks(session, false) == true, "Sticky locks should be enabled in the session by default");
      CHECK_MESSAGE(db->UseStickyLocks(false) == true, "Sticky locks should be enabled by default"); // <- the session change should not have affected the database

      // Now start the second transaction by reading another blob (the write lock on 0,0,0 will be released now)
      db->ReadBlob(0, 0, 1);
      syncPoint.wait(); // sync with other thread
      std::this_thread::sleep_for(std::chrono::milliseconds(100)); // just long enough to be sure the other thread is not blocked
      firstTransactionEnded = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will just open the database, wait for the first client to finish preparations and then attempt to acquire the previously write locked blob
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();

      // Try to read the first blob (which will not block as the other client still does not hold sticky locks)
      REQUIRE(db->ReadString(0, 0, 0) == "updated");
      secondReadCompleted = std::chrono::high_resolution_clock::now();
    }
  });


  REQUIRE_MESSAGE(secondReadCompleted.load() < firstTransactionEnded.load(), "The second client should be able to read the blob before the first client finishes");
}

// Basic test of behavior of Transaction::UseStickyLocks and Database::UseStickyLocks
TEST_CASE("UseStickyLocksSemantics") {
  auto session = Session::Create();
  database_ptr db1(Database::Open(session, "localhost/mem:useStickLocks_1"));
  REQUIRE_MESSAGE(db1->UseStickyLocks(true) == true, "The first database should use the session's default");

  REQUIRE_MESSAGE(Transaction::UseStickyLocks(session, false) == true, "The session's sticky lock mode should be initialized to enabled");
  REQUIRE_MESSAGE(db1->UseStickyLocks(true) == true, "The first database's sticky lock mode should not be affected by the session's mode change");

  database_ptr db2(Database::Open(session, "localhost/mem:useStickLocks_2"));
  REQUIRE_MESSAGE(db2->UseStickyLocks(true) == false, "The second database should use the session's new sticky lock mode upon opening");
}
