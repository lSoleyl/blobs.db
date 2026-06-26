#include "pch.hpp"

using namespace blobs;



/** This test will ensure that reading works as expected if the same databsae is reopened in mvcc inside the same session
 */
TEST_CASE("Test read MVCC in same session") {
  auto connStr = "localhost/mem:testMVCCreadSameSession.db";
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  // Prepare databse
  db->WriteString(0, 0, 0, "test");
  db->CreateString(0, 0, "test2");
  db->CreateCluster(0);
  Transaction::Commit(session);

  // Now test that we can read it in MVCC
  db->SetMVCC(true);
  REQUIRE(db->ReadString(0, 0, 0) == "test");
  REQUIRE(db->ReadString(0, 0, 1) == "test2");

  
  REQUIRE(intoVector(db->GetAllBlobs(0, 0)) == std::vector<blob_id> { 0, 1 });
  REQUIRE(intoVector(db->GetAllClusters(0)) == std::vector<cluster_id> { 0, 1 });
  REQUIRE(intoVector(db->GetAllSegments()) == std::vector<segment_id> { 0 });

  REQUIRE_NOTHROW_MESSAGE(Transaction::Commit(session), "Commit() should not throw as no writes have happened");
}

/** Same test as above, but here we will read in another session thus not using the session's database cache
 */
TEST_CASE("Test read MVCC in other session") {
  auto connStr = "localhost/mem:testMVCCreadOtherSession.db";
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  // Prepare databse
  db->WriteString(0, 0, 0, "test");
  db->CreateString(0, 0, "test2");
  db->CreateCluster(0);
  Transaction::Commit(session);

  {
    // Now read it in a second session that will open the database directly in MVCC mode
    auto session = Session::Create();
    database_ptr db(Database::OpenMVCC(session, connStr));

    REQUIRE(db->ReadString(0, 0, 0) == "test");
    REQUIRE(db->ReadString(0, 0, 1) == "test2");


    REQUIRE(intoVector(db->GetAllBlobs(0, 0)) == std::vector<blob_id> { 0, 1 });
    REQUIRE(intoVector(db->GetAllClusters(0)) == std::vector<cluster_id> { 0, 1 });
    REQUIRE(intoVector(db->GetAllSegments()) == std::vector<segment_id> { 0 });
  }
}


/** This test ensures that write operatiosn will fail on a client openeing the databse in MVCC mode
 */
TEST_CASE("Test write operations in MVCC") {
  auto connStr = "localhost/mem:testMVCCwrite.db";
  auto session = Session::Create();
  database_ptr db(Database::OpenMVCC(session, connStr));

  REQUIRE_THROWS_AS_MESSAGE(db->WriteString(0, 0, 0, "test"), exception::CannotWriteLockInMVCC, "WriteString should fail in MVCC");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(0, 0, "test"), exception::CannotWriteLockInMVCC, "CreateString should fail in MVCC");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateStringAt(0, 0, 2, "test"), exception::CannotWriteLockInMVCC, "CreateStringAt should fail in MVCC");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateCluster(0), exception::CannotWriteLockInMVCC, "CreateCluster should fail in MVCC");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateSegment(), exception::CannotWriteLockInMVCC, "CreateSegment should fail in MVCC");

  REQUIRE_THROWS_AS_MESSAGE(db->DeleteBlob(0, 0, 0), exception::CannotWriteLockInMVCC, "DeleteBlob should fail in MVCC");
  REQUIRE_THROWS_AS_MESSAGE(db->DeleteCluster(0, 0), exception::CannotWriteLockInMVCC, "DeleteCluster should fail in MVCC");
  REQUIRE_THROWS_AS_MESSAGE(db->DeleteSegment(0), exception::CannotWriteLockInMVCC, "DeleteSegment should fail in MVCC");

  REQUIRE_NOTHROW_MESSAGE(Transaction::Commit(session), "Commit() should not throw as no write should be committed");
}

/** Now test the main aspect of MVCC... We can read data while another client holds a write lock to the data we want to access
 */
TEST_CASE("Test read MVCC while other client holds write lock") {
  auto connStr = "localhost/mem:testMVCCreadDuringWriteLock.db";

  parallel::sync_point syncPoint(2);

  std::atomic<std::chrono::high_resolution_clock::time_point> mvccRead, updateCommit;

  parallel::run({
    // First client will open the database, write some data and then hold a write lock on the blob the second client wants to read.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));

      db->WriteString(0, 0, 0, "0,0,0-1");
      CHECK(db->CreateString(0, 0, "0,0,1-1") == 1);
      Transaction::Commit(session);
      // Now reacquire the write locks in a new transaction

      db->WriteString(0, 0, 0, "0,0,0-2");
      db->DeleteBlob(0, 0, 1);
      CHECK(db->CreateString(0, 0, "0,0,2-1") == 2);
      syncPoint.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect whether there is a lock or not
      updateCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // Second client starts its MVCC transaction while the first already holds its write locks
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));
      syncPoint.wait();

      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "0,0,0-1", "Expected to read the old string in MVCC transaction");
      CHECK_MESSAGE(db->ReadString(0, 0, 1) == "0,0,1-1", "Expected to be able to read the deleted blob in the MVCC transaction");
      CHECK_THROWS_AS_MESSAGE(db->ReadString(0, 0, 2), exception::BlobDoesNotExist, "Expected to not see the new blob in MVCC transaction");
      CHECK_MESSAGE(intoVector(db->GetAllBlobs(0, 0)) == (std::vector<blob_id> { 0, 1 }), "Expected other list of blobs in MVCC transaction");
      mvccRead = std::chrono::high_resolution_clock::now();
    }
  });

  REQUIRE_MESSAGE(mvccRead.load() < updateCommit.load(), "Expected MVCC client to not be blocked by update client holding write locks");
}




/** Now a similar test, but here we check that an update client can set a write lock, while an mvcc client has an active read transaction on the database
 */
TEST_CASE("Test update database while MVCC client reads data") {
  auto connStr = "localhost/mem:testMVCCwriteLockDuringRead.db";

  parallel::sync_point syncPoint(2);
  std::atomic<std::chrono::high_resolution_clock::time_point> mvccCommit, updateCommit;

  parallel::run({
    // First client will open the database and read data from it in MVCC.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));

      CHECK(db->ReadString(0, 0, 0) == "");
      CHECK(intoVector(db->GetAllBlobs(0, 0)) == std::vector<blob_id>{0});
      syncPoint.wait(); // wait here for the second client
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect whether there is a lock or not
      mvccCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // Second client starts a regular update transaction and attempts to write the blobs, which are being read inside an mvcc transaction at the moment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();

      db->WriteString(0, 0, 0, "test");
      db->CreateString(0, 0, "test2");
      
      updateCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    }
  });

  REQUIRE_MESSAGE(updateCommit.load() < mvccCommit.load(), "Expected update client to not be blocked by the read of an MVCC client");
}





/** This test case will simply check that LockMode:None will perform a dirty read even in MVCC and read the latest version of a blob
 */
TEST_CASE("Dirty Reads while in MVCC") {
  auto connStr = "localhost/mem:testMVCCdirtyRead.db";

  parallel::sync_point dbPrepared(2);
  parallel::sync_point mvccStarted(2);
  parallel::sync_point dbUpdated(2);

  parallel::run({
    // The first client will perpare the database and then perform a single update
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->WriteString(0, 0, 0, "1");
      Transaction::Commit(session);
      dbPrepared.wait();
      // Now update the value
      db->WriteString(0, 0, 0, "2");
      mvccStarted.wait(); // additional sync point to prevent WriteString+Commit to complete before ReadString() due to scheduling
      Transaction::Commit(session);
      dbUpdated.wait();
    },

    // The second client will open the databse in MVCC and perform regular reads and dirty reads while inside the same transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));
      dbPrepared.wait();
      CHECK(db->ReadString(0, 0, 0) == "1"); // Perform the first MVCC read to also start the MVCC transaction
      mvccStarted.wait();
      dbUpdated.wait();
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "1", "Re-reading in MVCC mode should return the same value even after another client changes the data");
      CHECK_MESSAGE(db->ReadString(0, 0, 0, Lock::None) == "2", "Reading the blob in MVCC mode with dirty read should return the updated value");
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "1", "Re-reading in MVCC mode should return the same value even after the dirty read");
      Transaction::Commit(session);
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "2", "Re-reading in MVCC in a new transaction should use the updated snapshot and return the updated value");
    }
  });
}


/** Similar to the above test, but here we will see that the snapshot won't be updated, because another MVCC client will reference it.
 */
TEST_CASE("Snapshot update with multiple MVCC clients") {
  auto connStr = "localhost/mem:testMVCCsnapshotUpdate.db";

  parallel::sync_point dbPrepared(3);
  parallel::sync_point mvccStarted(3);
  parallel::sync_point dbUpdated(2);
  parallel::sync_point secondClientComplete(2);
  parallel::sync_point secondClientCommitted(2);

  parallel::run({
    // The first client will perpare the database and then perform a single update
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->WriteString(0, 0, 0, "1");
      Transaction::Commit(session);
      dbPrepared.wait();
      // Now update the value
      db->WriteString(0, 0, 0, "2");
      mvccStarted.wait(); // additional sync point to prevent WriteString+Commit to complete before ReadString() due to scheduling
      Transaction::Commit(session);
      dbUpdated.wait();
    },

    // The second client will open the database in MVCC and then just keep the transaction open until the end (thus keeping the snapshot alive)
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));
      dbPrepared.wait();
      CHECK(db->ReadString(0, 0, 0) == "1");
      mvccStarted.wait();
      secondClientComplete.wait(); // keep the transaction running until thrid client 
      Transaction::Commit(session);
      secondClientCommitted.wait(); // the third client will restart its transaction after this point to refresh the snapshot
    },


    // The third client will first read 
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));
      dbPrepared.wait();
      CHECK(db->ReadString(0, 0, 0) == "1");
      mvccStarted.wait();
      Transaction::Commit(session); // immediately end the transaction
      dbUpdated.wait();
      // And re-read the string in the next transaction after the update
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "1", "The third client should still read the old value, because second client holds the MVCC snapshot");

      // Now let the second client end its transaction
      secondClientComplete.wait();
      secondClientCommitted.wait();
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "1", "Re reading the updated blob in the same MVCC transaction should still return the old value");
      Transaction::Commit(session);
      
      // Now start a third transaction AFTER the update and AFTER the second client ended its MVCC transaction - This should start an MVCC session with an updated snapshot
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "2", "Reading the updated blob in a transaction AFTER the last MVCC transaction finished should read the updated value");
    }
  });
}


/** This test ensures that a blob that is just overwritten will be fully loaded from file if the database has an active MVCC snapshot
 */
TEST_CASE("Overwritten blobs should be loaded from file with an MVCC snapshot") {
  // IMPORTANT: we must use a file database for this test to have any meaning
  auto connStr = "localhost/testMVCCupdateFileBlob.db";

  // First prepare the file database in a separate session
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr, Database::OpenMode::CreateAlways)); // open mode is important here to always start the test with a fresh DB
    db->WriteString(0, 0, 0, "000");
    db->CreateSegment();
    db->WriteString(1, 0, 0, "100"); // we need a second blob to start the MVCC transaction by reading it
    Transaction::Commit(session);
  }

  parallel::sync_point mvccStarted(2);
  parallel::sync_point blobUpdated(2);


  parallel::run({
    // The first client will update the database in an update session, wait for the MVCC transaction to start and then directly update the blob without
    // reading it first. Without the MVCC snapshot present, the server could just discard that blob during commit, but with the MVCC snapshot, the blob
    // must first be read into memory.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      mvccStarted.wait();
      db->WriteString(0, 0, 0, "updated");
      Transaction::Commit(session);
      blobUpdated.wait();
    },

    // The second client will start an MVCC transaction, wait for the update transaction to complete and then read the updated blob.
    // We expect to read the not yet updated value.
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));
      CHECK(db->ReadString(1, 0, 0) == "100");
      mvccStarted.wait();
      blobUpdated.wait();
      CHECK_MESSAGE(db->ReadString(0, 0, 0) == "000", "MVCC client should read the original blob value after the update client committed");
    }
  });
}


/** This test ensures that (if we have an active MVCC snapshot) we will not just delete not yet loaded blobs,clusters,segments
 *  as they could later be accessed inside that MVCC transaction.
 */
TEST_CASE("Loading Blobs,Clusters,Segments from file before deletion when MVCC is active") {
  // IMPORTANT: we must use a file database for this test to have any meaning
  auto connStr = "localhost/testMVCCdeleteBCS.db";

  // First prepare the file database in a separate session
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr, Database::OpenMode::CreateAlways)); // open mode is important here to always start the test with a fresh DB
    db->WriteString(0, 0, 0, "000");
    db->CreateString(0, 0, "001");
    db->CreateSegment();
    db->WriteString(1, 0, 0, "100");
    db->CreateCluster(0);
    db->WriteString(0, 1, 0, "010");
    Transaction::Commit(session);
  }

  // Now the file database is closed and unloaded by the server
  parallel::sync_point mvccStarted(2);
  parallel::sync_point dataDeleted(2);

  parallel::run({
    // The first client will simply reopen the database, wait for MVCC transaction to start and then delete all the (not yet loaded blob,cluster,segments)
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      mvccStarted.wait();
      db->DeleteBlob(0, 0, 1);
      db->DeleteCluster(0, 1);
      db->DeleteSegment(1);
      Transaction::Commit(session);
      dataDeleted.wait();
    },

    // The second client will open the database in MVCC and will read the deleted blobs from the snapshot AFTER the update client commits its changes
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::OpenMVCC(session, connStr));
      CHECK(db->ReadString(0, 0, 0) == "000");
      mvccStarted.wait(); // let the second client delete the data
      dataDeleted.wait(); // wait for second client to commit the deletes
      
      // Now read the deleted data
      CHECK_MESSAGE(db->ReadString(0, 0, 1) == "001", "The deleted blob should be readable in MVCC");
      CHECK_MESSAGE(db->ReadString(0, 1, 0) == "010", "The deleted cluster should be readable in MVCC");
      CHECK_MESSAGE(db->ReadString(1, 0, 0) == "100", "The deleted segment should be readable in MVCC");
    }
  });
}


/** This test case will validate that OpenDB -> SetMVCC will not work while already inside a transaction, but OpenMVCC will work correctly
 */
TEST_CASE("Test OpenMVCC while in txn") {
  auto connStrA = "localhost/mem:testOpenMVCCInTxnA";
  auto connStrB = "localhost/mem:testOpenMVCCInTxnB";

  std::atomic<std::chrono::high_resolution_clock::time_point> client1Done, client2Done, client3Done;
  parallel::sync_point syncPoint(3);

  parallel::run({
    // The first client will use OpenMVCC, read blob 0 and finish first
    [&]() {
      auto session = Session::Create();
      database_ptr dbA(Database::Open(session, connStrA));
      CHECK(dbA->ReadString(0, 0, 0) == ""); // Just to start a transaction

      // Open second database in MVCC mode directly
      database_ptr dbB(Database::OpenMVCC(session, connStrB));
      syncPoint.wait();

      // Now reading should return immediately since we can read lock free from the MVCC snapshot taken when the database was opened.
      CHECK_MESSAGE(dbB->ReadString(0, 0, 0) == "", "Client 1 should not see the data committed by Client 2 yet");
      client1Done = std::chrono::high_resolution_clock::now();
    },

    // The second client will open only the second database normally and write lock blob 0
    [&]() {
      auto session = Session::Create();
      database_ptr dbB(Database::Open(session, connStrB));
      dbB->WriteString(0, 0, 0, "client2");
      syncPoint.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the lock
      client2Done = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The third client will open the database and try to SetMVCC and then read blob 0, which will be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr dbA(Database::Open(session, connStrA));
      CHECK(dbA->ReadString(0, 0, 0) == ""); // Just to start a transaction

      // Now open the second database and directly set it to MVCC (will not actually be applied to this txn)
      database_ptr dbB(Database::Open(session, connStrB));
      dbB->SetMVCC(true);
      syncPoint.wait();
      CHECK_MESSAGE(dbB->ReadString(0, 0, 0) == "client2", "Client 3 should actually see the committed data from Client 2");
      client3Done = std::chrono::high_resolution_clock::now();
    }
  });


  REQUIRE_MESSAGE(client1Done.load() < client2Done.load(), "The OpenMVCC client should finish before the regular update client");
  REQUIRE_MESSAGE(client2Done.load() < client3Done.load(), "The Open+SetMVCC client shoudl finish after the regular update client");
}



