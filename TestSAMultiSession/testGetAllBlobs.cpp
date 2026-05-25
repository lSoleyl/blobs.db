#include "pch.hpp"


using namespace blobs;

// This test will ensure that the list can be correctly queried and is correctly adjusted to the modifications
// performed during a transaction and that it is correctly read across transaction.
TEST_CASE("GetAllBlobs tests with on default cluster") {
  auto session = Session::Create();
  auto dbName = "mem:testGetAllBlobsDefault";
  database_ptr db(Database::Open(session, "localhost", dbName));

  auto blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == std::vector<blob_id>{0}, "Expected other blob list for initial state of default cluster in default segment");

  // Now delete and create blobs in the same transaction
  db->DeleteBlob(0, 0, 0); // default blob
  blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == std::vector<blob_id>{}, "Expected no blobs after deleting the default blob");
  REQUIRE(db->CreateString(0, 0, "1") == 1);
  REQUIRE(db->CreateString(0, 0, "2") == 2);

  blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{1, 2}), "Expected 1,2");

  // Now test whether two ranges of blob ids are handled correctly
  REQUIRE(db->CreateString(0, 0, "3") == 3);
  REQUIRE(db->CreateString(0, 0, "4") == 4);
  REQUIRE(db->CreateString(0, 0, "5") == 5);

  blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{1, 2, 3, 4, 5}), "Expected 1,2,3,4,5");

  // Now delete the blob in the middle to create two ranges
  db->DeleteBlob(0, 0, 3);
  blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{1, 2, 4, 5}), "Expected 1,2,4,5");

  // Now abort and verify that the list has been reverted
  Transaction::Abort(session);
  blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{0}), "Expected 0 after aborting the transaction");

  // Create blobs and delete one
  REQUIRE(db->CreateString(0, 0, "1") == 1);
  REQUIRE(db->CreateString(0, 0, "2") == 2);
  REQUIRE(db->CreateString(0, 0, "3") == 3);
  REQUIRE(db->CreateString(0, 0, "4") == 4);
  db->DeleteBlob(0, 0, 2);

  // This time commit the changes
  Transaction::Commit(session); 

  blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{0,1,3,4}), "Expected 0,1,3,4 in second transaction");
}


// Here we ensure that the list will be retrieved in the correct state even if we first perform modifications that affect the list
// and then request it from the server
TEST_CASE("Delete and create blob then GetAllBlobs") {
  auto session = Session::Create();
  auto dbName = "mem:testModifyListThenGetAllBlobs";
  database_ptr db(Database::Open(session, "localhost", dbName));

  db->DeleteBlob(0, 0, 0);
  REQUIRE(db->CreateString(0, 0, "1") == 1);
  REQUIRE(db->CreateString(0, 0, "2") == 2);
  REQUIRE(db->CreateString(0, 0, "3") == 3);
  REQUIRE(db->CreateString(0, 0, "4") == 4);
  db->DeleteBlob(0,0,3); // this blob hasn't been committed yet


  auto blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{1, 2, 4}), "Expected 1,2,4 when requested after modifications");
  Transaction::Commit(session);

  blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{1, 2, 4}), "Expected 1,2,4 when requested after committing the first transaction");
}


// This test ensures correct locking semantics for the blob id list in respect to other operations
TEST_CASE("Querying all blobs should block blob creation/deletion and cluster deletion, but not cluster creation") {
  const auto dbName = "mem:testGetAllBlobsLockingSemantics";
  database_ptr db(Database::Open("localhost", dbName));
  REQUIRE(db->CreateString(0, 0, "1") == 1); // prepare database
  Transaction::Commit();

  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientCommit, blobCreated, blobDeleted, clusterDeleted, clusterCreated, blobWritten;

  parallel::sync_point syncPoint(6);
  parallel::run({
    // The first client will simply query the blob list of the default cluster in the default segment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      auto range = db->GetAllBlobs(0, 0);
      syncPoint.wait();

      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the lock
      firstClientCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will attempt to create a blob in the default cluster
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateString(0, 0, "2") == 2);
      blobCreated = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The third client will attempt to delete a blob in the default cluster
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->DeleteBlob(0, 0, 0);
      blobDeleted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },
    
    // The fourth client will attempt to delete the cluster, but will abort that transaction to not impact clients two and three
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->DeleteCluster(0, 0);
      clusterDeleted = std::chrono::high_resolution_clock::now();
      Transaction::Abort(session);
    },

    // The fifth client will simply create a new cluster, which should not be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateCluster(0) == 1);
      clusterCreated = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The sixth client will simply write a blob from the cluster, which should not be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->WriteString(0, 0, 1, "xxx");
      blobWritten = std::chrono::high_resolution_clock::now();
      Transaction::Commit();
    }
  });

  
  REQUIRE_MESSAGE(firstClientCommit.load() < blobDeleted.load(), "Reading the blob list should block blob deletion in that cluster");
  REQUIRE_MESSAGE(firstClientCommit.load() < blobCreated.load(), "Reading the blob list should block blob creation in that cluster");
  REQUIRE_MESSAGE(firstClientCommit.load() < clusterDeleted.load(), "Reading the blob list should block the cluster's deletion");

  REQUIRE_MESSAGE(clusterCreated.load() < firstClientCommit.load(), "Creating a new cluster should not be blocked by reading the blob list of another cluster");
  REQUIRE_MESSAGE(blobWritten.load() < firstClientCommit.load(), "Writing a blob inside the cluster should not be blocked by reading the blob list");
}

// Tests for operations that should block querying the blob id list
TEST_CASE("Querying blob list") {
  std::atomic<std::chrono::high_resolution_clock::time_point> readListCompleted, blockingTransactionCompleted;
  parallel::sync_point syncPoint(2);

  SUBCASE("should be blocked by another client creating a blob in the same cluster") {
    const auto dbName = "mem:GetAllBlobsLockingSemantics2";

    parallel::run({
      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        CHECK(db->CreateString(0, 0, "1") == 1);
        syncPoint.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect the blocking
        blockingTransactionCompleted = std::chrono::high_resolution_clock::now();
        Transaction::Commit(session);
      },

      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        syncPoint.wait();
        auto blobs = intoVector(db->GetAllBlobs(0, 0));
        readListCompleted = std::chrono::high_resolution_clock::now();
        CHECK(blobs == std::vector<blob_id>{0, 1});
      }
    });

    REQUIRE_MESSAGE(blockingTransactionCompleted.load() < readListCompleted.load(), "GetAllBlobs() should be blocked by blob creation in the same cluster");
  }


  SUBCASE("should be blocked by another client deleting a blob in the same cluster") {
    const auto dbName = "mem:GetAllBlobsLockingSemantics2";

    parallel::run({
      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        db->DeleteBlob(0, 0, 0);
        syncPoint.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect the blocking
        blockingTransactionCompleted = std::chrono::high_resolution_clock::now();
        Transaction::Commit(session);
      },

      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        syncPoint.wait();
        auto blobs = intoVector(db->GetAllBlobs(0, 0));
        readListCompleted = std::chrono::high_resolution_clock::now();
        CHECK(blobs == std::vector<blob_id>{});
      }
      });

    REQUIRE_MESSAGE(blockingTransactionCompleted.load() < readListCompleted.load(), "GetAllBlobs() should be blocked by blob deletion in the same cluster");
  }
}


// This test will ensure that cluster creation will implcitly grant a lock (transferred as sticky lock into the next transaction) on the 
// BlobListId of the created cluster.
TEST_CASE("Create cluster sticky lock on BlobListId") {
  const auto dbName = "mem:testCreateClusterLockBlobList";
  parallel::sync_point syncPoint(2);

  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientCommit, secondClientReadBlobs;
  parallel::run({
    // The first client will create a cluster, commit and then start a new transaction (thus holding the lock on the blob list)
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK(db->CreateCluster(0) == 1);
      Transaction::Commit(session);
      
      // Start a new transaction by reading an unrelated blob
      CHECK(db->ReadString(0, 0, 0) == "");
      syncPoint.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the lock
      firstClientCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will wait for the first client to finish preparations and then attempt to simply read the blob list of the created cluster
    // This should be blocked for as long as the first client doesn't commit his transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      auto blobs = intoVector(db->GetAllBlobs(0, 1));
      secondClientReadBlobs = std::chrono::high_resolution_clock::now();
      CHECK(blobs == std::vector<blob_id>{0}); // Only the default blob should exist in the newly created cluster
    }
  });

  REQUIRE_MESSAGE(firstClientCommit.load() < secondClientReadBlobs.load(), "The second client's read should have been blocked until the first client completes its transaction");
}


// This test will ensure that segment creation will implcitly grant a lock (transferred as sticky lock into the next transaction) on the 
// BlobListId of the default cluster of the created segment.
TEST_CASE("Create cluster sticky lock on BlobListId") {
  const auto dbName = "mem:testCreateSegmentLockBlobList";
  parallel::sync_point syncPoint(2);

  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientCommit, secondClientReadBlobs;
  parallel::run({
    // The first client will create a cluster, commit and then start a new transaction (thus holding the lock on the blob list)
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK(db->CreateSegment() == 1);
      Transaction::Commit(session);

      // Start a new transaction by reading an unrelated blob
      CHECK(db->ReadString(0, 0, 0) == "");
      syncPoint.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the lock
      firstClientCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will wait for the first client to finish preparations and then attempt to simply read the blob list of the default cluster
    // in the created segment. This should be blocked for as long as the first client doesn't commit his transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      auto blobs = intoVector(db->GetAllBlobs(1, 0));
      secondClientReadBlobs = std::chrono::high_resolution_clock::now();
      CHECK(blobs == std::vector<blob_id>{0}); // Only the default blob should exist in the newly created cluster
    }
    });

  REQUIRE_MESSAGE(firstClientCommit.load() < secondClientReadBlobs.load(), "The second client's read should have been blocked until the first client completes its transaction");
}


TEST_CASE("GetAllBlobs after CreateCluster in same transaction") {
  const auto dbName = "mem:testGetAllBlobsAfterCreateCluster";
  auto session = Session::Create();

  database_ptr db(Database::Open(session, "localhost", dbName));
  auto cluster = db->CreateCluster(0);
  REQUIRE_MESSAGE(cluster == 1, "Newly created cluster has wrong id");

  blobs::Range<blob_id> blobs;
  REQUIRE_NOTHROW(blobs = db->GetAllBlobs(0, cluster));
  REQUIRE_MESSAGE(blobs.size() == 1, "The returned blob range should contain one blob");
  REQUIRE_MESSAGE(*blobs.begin() == 0, "The new cluster should only consist of the blob 0");
}

TEST_CASE("GetAllBlobs after CreateSegment in same transaction") {
  const auto dbName = "mem:testGetAllBlobsAfterCreateSegment";
  auto session = Session::Create();

  database_ptr db(Database::Open(session, "localhost", dbName));
  auto segment = db->CreateSegment();
  REQUIRE_MESSAGE(segment == 1, "Newly created segment has wrong id");

  blobs::Range<blob_id> blobs;
  REQUIRE_NOTHROW(blobs = db->GetAllBlobs(segment, 0));
  REQUIRE_MESSAGE(blobs.size() == 1, "The returned blob range should contain one blob");
  REQUIRE_MESSAGE(*blobs.begin() == 0, "The default cluster in the new segment should only consist of the blob 0");
}


TEST_CASE("GetAllBlobs oder after recreating deleted blob in new transaction") {
  const auto connStr = "localhost/mem:testGetAllBlobsOrderAfterRecreateBlob";
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  // Prepare database
  db->WriteString(0, 0, 0, "0");
  REQUIRE(db->CreateString(0, 0, "1") == 1);
  REQUIRE(db->CreateString(0, 0, "2") == 2);
  db->DeleteBlob(0, 0, 1); // we want to leave a gap in the blob numbering

  auto blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{0, 2}), "Wrong blobs list during setup");
  Transaction::Commit(session);


  // Now perform the actual test in a separate session
  // Recreate blob 1 and query the blob list in the same transaction
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr));
    db->CreateStringAt(0, 0, 1, "11"); // Re-create blob 1

    // We expect the blob list to be correctly sorted even though we didn't commit blob 1 to the server yet
    auto blobs = intoVector(db->GetAllBlobs(0, 0));
    REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{0, 1, 2}), "Wrong blobs list after delete");
  }
}

TEST_CASE("GetAllBlobs after deleting and recreating blob in same transaction") {
  const auto connStr = "localhost/mem:testGetAllBlobsAfterRecreateBlob";
  auto session = Session::Create();
  database_ptr db(Database::Open(session, connStr));

  // Prepare database
  db->WriteString(0, 0, 0, "0");
  REQUIRE(db->CreateString(0, 0, "1") == 1);
  REQUIRE(db->CreateString(0, 0, "2") == 2);

  auto blobs = intoVector(db->GetAllBlobs(0, 0));
  REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{0, 1, 2}), "Wrong blobs list during setup");
  Transaction::Commit(session);


  // Now perform the actual test in a separate session
  // Delete and recreate blob 1 in the same transaction
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, connStr));
    db->DeleteBlob(0, 0, 1);

    auto blobs = intoVector(db->GetAllBlobs(0, 0));
    REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{0, 2}), "Wrong blobs list after delete");

    db->CreateStringAt(0, 0, 1, "11");

    // Important: The ordering should be correct!
    blobs = intoVector(db->GetAllBlobs(0, 0));
    REQUIRE_MESSAGE(blobs == (std::vector<blob_id>{0, 1, 2}), "Wrong blobs list after re-create blob");
  }
}