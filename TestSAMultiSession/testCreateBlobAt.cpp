#include "pch.hpp"

using namespace blobs;




/** The following tests will ensure proper behavior of CreateBlobAt
 */


TEST_CASE("CreateBlobAt blob creation") {
  auto connStr = "localhost/mem:testCreateBlobAtCreation.db";
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));


  REQUIRE_MESSAGE(db->CreateString(0, 0, "1") == 1, "First regular created blob has wrong id");
  REQUIRE(db->ReadString(0, 0, 1) == "1");

  REQUIRE_NOTHROW(db->CreateStringAt(0, 0, 5, "5"));
  REQUIRE(db->ReadString(0, 0, 5) == "5");

  REQUIRE_NOTHROW(db->CreateStringAt(0, 0, 3, "3"));
  REQUIRE(db->ReadString(0, 0, 3) == "3");

  // Now regular creation
  REQUIRE_MESSAGE(db->CreateString(0, 0, "6") == 6, "Expected other blob id returned from regular blob creation");
  REQUIRE(db->ReadString(0, 0, 6) == "6");

  REQUIRE_NOTHROW(db->CreateStringAt(0, 0, 7, "7"));
  REQUIRE(db->ReadString(0, 0, 7) == "7");

  REQUIRE_MESSAGE(db->CreateString(0, 0, "8") == 8, "Default creation should return blob 8 after manually creating blob 7");
  REQUIRE(db->ReadString(0, 0, 8) == "8");

  REQUIRE_NOTHROW(db->CreateStringAt(0, 0, 2, "2"));
  REQUIRE(db->ReadString(0, 0, 2) == "2");


  REQUIRE_THROWS_AS(db->CreateStringAt(0, 0, 1, "1x"), exception::BlobAlreadyExists);
  REQUIRE_THROWS_AS(db->CreateStringAt(0, 0, 2, "2x"), exception::BlobAlreadyExists);


  // Now delete a blob created in the same transaction
  REQUIRE_NOTHROW_MESSAGE(db->DeleteBlob(0, 0, 1), "Deleting the default created blob in the same transaction should not fail");
  REQUIRE_NOTHROW_MESSAGE(db->DeleteBlob(0, 0, 2), "Deleting a directly created blob in the same transaction should not fail");
  REQUIRE_NOTHROW_MESSAGE(db->DeleteBlob(0, 0, 3), "Deleting a directly created blob in the same transaction should not fail");

  // Re-Creating these blobs in the same transaction should not fail
  REQUIRE_NOTHROW_MESSAGE(db->CreateStringAt(0, 0, 1, "11"), "Recreating blob 1 in the same transaction should not fail");
  REQUIRE_NOTHROW_MESSAGE(db->CreateStringAt(0, 0, 2, "22"), "Recreating blob 2 in the same transaction should not fail");

  REQUIRE_NOTHROW(Transaction::Commit(session));


  // Now delete a blob in the next transaction and re-create a previously deleted blob
  REQUIRE_NOTHROW_MESSAGE(db->DeleteBlob(0, 0, 5), "Deleting a blob in the next transaction should not fail");
  REQUIRE_NOTHROW_MESSAGE(db->CreateStringAt(0, 0, 3, "33"), "Re-Creating a blob deleted in a previous transaction should not fail");


  REQUIRE_THROWS_AS_MESSAGE(db->CreateStringAt(0, 0, 2, "2x"), exception::BlobAlreadyExists, "Attempt to create a blob which has been created in a previous transaction should fail");
  REQUIRE_NOTHROW(Transaction::Commit(session));

  // Now validate the database state in a separate session
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr));

    REQUIRE(db->ReadString(0, 0, 1) == "11");
    REQUIRE(db->ReadString(0, 0, 2) == "22");
    REQUIRE(db->ReadString(0, 0, 3) == "33");
    REQUIRE_THROWS_AS(db->ReadString(0, 0, 4), exception::BlobDoesNotExist);
    REQUIRE_THROWS_AS(db->ReadString(0, 0, 5), exception::BlobDoesNotExist);
    REQUIRE(db->ReadString(0, 0, 6) == "6");
    REQUIRE(db->ReadString(0, 0, 7) == "7");
    REQUIRE(db->ReadString(0, 0, 8) == "8");
    REQUIRE_THROWS_AS(db->ReadString(0, 0, 9), exception::BlobDoesNotExist);
  }
}



TEST_CASE("CreateBlobAt blob creation") {
  auto connStr = "localhost/mem:testCreateBlobAtSynchronization.db";


  parallel::sync_point syncPoint(5);
  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientCommit, secondClient, thirdClient, fourthClient, fifthClient;

  parallel::run({
    // The first client will block all other clients for the duration of its transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));

      db->CreateStringAt(0, 0, 7, "7");
      syncPoint.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the lock
      firstClientCommit = std::chrono::high_resolution_clock::now();
      REQUIRE_NOTHROW(Transaction::Commit(session));
    },

    // The second client will attempt to default create a blob
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();

      // We cannot really make any safe assumption whether this will be 8 or 11 as it is entirely dependent 
      // on the os's scheduling of threads
      db->CreateString(0, 0, "default");
      secondClient = std::chrono::high_resolution_clock::now();
      REQUIRE_NOTHROW(Transaction::Commit(session));
    },


    // The third client will attempt to create a blob in the range of already existing ones
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();

      db->CreateStringAt(0, 0, 3, "3");
      thirdClient = std::chrono::high_resolution_clock::now();
      REQUIRE_NOTHROW(Transaction::Commit(session));
    },

    // The fourth client will attempt to create a blob outside of the range of already existing blobs
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();

      db->CreateStringAt(0, 0, 10, "10");
      fourthClient = std::chrono::high_resolution_clock::now();
      REQUIRE_NOTHROW(Transaction::Commit(session));
    },

    // The fifth client will attempt to read the blob list
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();

      auto blobs = db->GetAllBlobs(0, 0);
      // we cannot make any assumptions about the exact state of existing blobs as it is entirely dependent on the os's scheduling
      fifthClient = std::chrono::high_resolution_clock::now();
      REQUIRE_NOTHROW(Transaction::Commit(session));
    }
    });


  // The only important part is that all clients are scheduled AFTER the first one
  REQUIRE(firstClientCommit.load() < secondClient.load());
  REQUIRE(firstClientCommit.load() < thirdClient.load());
  REQUIRE(firstClientCommit.load() < fourthClient.load());
  REQUIRE(firstClientCommit.load() < fifthClient.load());
}




TEST_CASE("CreateBlobAt sticky lock transfer") {
  auto connStr = "localhost/mem:testCreateBlobAtStickyLocks.db";

  // Prepare database
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));
  db->CreateStringAt(0, 0, 7, "7");
  REQUIRE_NOTHROW(Transaction::Commit(session));

  parallel::sync_point syncPoint(5);
  std::atomic<std::chrono::high_resolution_clock::time_point> firstClient, secondClient, thirdClient, fourthClient, fifthClient;

  parallel::run({
    // First client will create a blob, commit and then start another transaction and hold on the the sticky locks from blob creation
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      db->CreateStringAt(0, 0, 3, "3"); // <- this should transfer sticky lock for blob 3, NextFreeBlobId and BlobListId
      REQUIRE_NOTHROW(Transaction::Commit(session));

      // Read blob 0 to start another transaction
      CHECK(db->ReadString(0, 0, 0) == "");
      syncPoint.wait();

      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measrue the lock
      firstClient = std::chrono::high_resolution_clock::now();
      REQUIRE_NOTHROW(Transaction::Commit(session));
    },

    // Second client will attempt to create blob 4
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();
      db->CreateStringAt(0, 0, 4, "4");
      secondClient = std::chrono::high_resolution_clock::now();
    },

    // Third client will attempt to read blob 3
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();
      CHECK(db->ReadString(0, 0, 3) == "3");
      thirdClient = std::chrono::high_resolution_clock::now();
    },


    // Fourth client will attempt to read the blob id list
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();
      auto range = db->GetAllBlobs(0, 0);
      fourthClient = std::chrono::high_resolution_clock::now();
    },


    // Fifth client will attempt to read a non existing blob (that is not locked)
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, connStr));
      syncPoint.wait();

      CHECK(db->ReadString(0, 0, 7) == "7");
      CHECK_THROWS_AS(db->ReadString(0, 0, 5), exception::BlobDoesNotExist);
      fifthClient = std::chrono::high_resolution_clock::now();
    },
    });


  REQUIRE(fifthClient.load() < firstClient.load()); // The fifth client should not be blocked by the sticky locks of the first one
  REQUIRE(firstClient.load() < secondClient.load());
  REQUIRE(firstClient.load() < thirdClient.load());
  REQUIRE(firstClient.load() < fourthClient.load());
}



TEST_CASE("CreateBlobAt in non existent cluster") {
  auto connStr = "localhost/mem:testCreateBlobAtNonExistingCluster.db";
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  REQUIRE_THROWS_AS(db->CreateStringAt(0, 1, 1, "test"), exception::BlobDoesNotExist);
}



TEST_CASE("CreateBlobAt in newly created cluster") {
  auto connStr = "localhost/mem:testCreateBlobAtNewCluster.db";
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  REQUIRE(db->CreateCluster(0) == 1);


  REQUIRE_THROWS_AS_MESSAGE(db->CreateStringAt(0, 1, 0, "0x"), exception::BlobAlreadyExists, "Creating blob 0 in newly created cluster should fail");
  REQUIRE_NOTHROW_MESSAGE(db->CreateStringAt(0, 1, 1, "1"), "Creating blob 1 in newly created cluster should not fail");
  REQUIRE_MESSAGE(db->CreateString(0, 1, "2") == 2, "Default creating a blob in newly created cluster should return blob id 2");
  REQUIRE_NOTHROW_MESSAGE(db->CreateStringAt(0, 1, 7, "7"), "Creating blob 7 in newly created cluster should not fail");
  REQUIRE_MESSAGE(db->CreateString(0, 1, "8") == 8, "Default creating a blob in newly created cluster should return blob id 8");
  REQUIRE_NOTHROW(Transaction::Commit(session));


  // Now vaidate the contents of the cluster
  REQUIRE(db->ReadString(0, 1, 0) == "");
  REQUIRE(db->ReadString(0, 1, 1) == "1");
  REQUIRE(db->ReadString(0, 1, 2) == "2");
  REQUIRE(db->ReadString(0, 1, 7) == "7");
  REQUIRE(db->ReadString(0, 1, 8) == "8");
}