#include "pch.hpp"

using namespace blobs;


// This test will ensure that a client, which attempts to delete a cluster will not be able to do so for 
// as long as another client still holds a lock to any blob inside the cluster
TEST_CASE("Locking semantics during cluster deletion") {
  auto dbName = "mem:lockingDuringClusterDeletion";
  database_ptr db(Database::Open("localhost", dbName));
  cluster_id createdCluster = db->CreateCluster(0);
  db->WriteString(0, createdCluster, 0, "created");
  Transaction::Commit();
  
  parallel::sync_point beforeDelete(2);
  parallel::sync_point afterDelete(2);


  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientClusterDeleted, firstClientBeforeCommit;
  std::atomic<std::chrono::high_resolution_clock::time_point> secondClientBeforeCommit;
  std::atomic<std::chrono::high_resolution_clock::time_point> thirdClientBlobRead;


  parallel::run({
    // The main thread will simply attempt to delete the created cluster
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));

      beforeDelete.wait();
      db->DeleteCluster(0, createdCluster);
      firstClientClusterDeleted = std::chrono::high_resolution_clock::now();

      CHECK_THROWS_AS_MESSAGE(db->WriteString(0, createdCluster, 0, "test"), exception::ClusterDeleted, "Updating a blob after deleting its cluster should throw");

      afterDelete.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the blocking of the third thread
      firstClientBeforeCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second thread will read the only blob inside the cluster to delete and then wait for a moment to block the main thread
    // from proceeding with the cluster's deletion to ensure that a lock inside the cluster will prevent the cluster from being deleted
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK(db->ReadString(0, createdCluster, 0) == "created");
      beforeDelete.wait(); // sync with main thread
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the blocking of the main thread
      secondClientBeforeCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The third thread will attempt to read a blob inside the cluster AFTER the main thread has called DeleteCluster(), which should block 
    // until the main thread finishes its transaction and then throw an exception due to deleted cluster
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      afterDelete.wait();
      // The following read should now block until the main thread completes the transaction
      CHECK_THROWS_AS_MESSAGE(db->ReadString(0, createdCluster, 0), exception::BlobDoesNotExist, "Expected exception to be thrown when reading from a deleted cluster");
      thirdClientBlobRead = std::chrono::high_resolution_clock::now();
    }
  });

  REQUIRE_MESSAGE(secondClientBeforeCommit.load() < firstClientClusterDeleted.load(), "The main thread should have been blocked from deleting the cluster by the second thread");
  REQUIRE_MESSAGE(firstClientBeforeCommit.load() < thirdClientBlobRead.load(), "The third thread has to wait for the main thread to complete");
}


// This test will ensure that another client will be blocked while main client attempts to delete a cluster, but 
// will be able to access the cluster normally as soon as the main client aborts its transaction
TEST_CASE("Locking and access semantics during cluster deletion abort in other client") {
  auto dbName = "mem:lockingDuringClusterDeletionAbort";
  database_ptr db(Database::Open("localhost", dbName));
  cluster_id createdCluster = db->CreateCluster(0);
  db->WriteString(0, createdCluster, 0, "created");
  Transaction::Commit();

  parallel::sync_point afterDelete(2);


  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientBeforeAbort;
  std::atomic<std::chrono::high_resolution_clock::time_point> secondClientBlobRead;


  parallel::run({
    // The main thread will simply attempt to delete the created cluster and Abort the transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      db->DeleteCluster(0, createdCluster);
      afterDelete.wait();

      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the blocking of the third thread
      firstClientBeforeAbort = std::chrono::high_resolution_clock::now();
      Transaction::Abort(session);
    },

    // The second thread will attempt to read a blob in the cluster that is marked for deletion and also attempt to create a new blob
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      afterDelete.wait(); // sync with main thread

      CHECK(db->ReadString(0, createdCluster, 0) == "created");
      secondClientBlobRead = std::chrono::high_resolution_clock::now();
      CHECK_NOTHROW_MESSAGE(db->CreateString(0, createdCluster, "new"), "Creating a blob in the cluster should be allowed if the transaction was aborted");
      Transaction::Commit(session);
    }
  });

  REQUIRE_MESSAGE(firstClientBeforeAbort.load() < secondClientBlobRead.load(), "The main thread should have blocked the second client from reading a blob in the cluster");
  REQUIRE(db->ReadString(0, createdCluster, 0) == "created");
  REQUIRE(db->ReadString(0, createdCluster, 1) == "new");
}


// This single session test case checks three things:
//  1. the client attempting to delete the cluster can do so even if they already accessed the cluster before
//  2. the cluster cannot be read from / written to after the call to DeleteCluster()
//  3. a blob inside that cluster can also not be accessed after the transaction commits
TEST_CASE("Deleting a cluster when already holding locks into it") {
  auto dbName = "mem:deleteClusterCommitBlobAccess";
  // Prepare the database in the main session
  database_ptr mainDb(Database::Open("localhost", dbName));
  auto clusterId = mainDb->CreateCluster(0);
  mainDb->WriteString(0, clusterId, 0, "init");
  Transaction::Commit();

  // Now run the test in a separate session (to avoid affecting the main session)
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", dbName));
  // Read a blob in the cluster
  REQUIRE_MESSAGE(db->ReadString(0, clusterId, 0) == "init", "Initial blob value is wrong");

  // And in the same transaction delete the cluster now the above read lock should not hinder us from deleting the cluster
  db->DeleteCluster(0, clusterId);

  // Reading the same blob and creating new blobs should both throw (should be handled in the client)
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(0, clusterId, 0), exception::ClusterDeleted, "Reading before committing DeleteCluster should throw");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(0, clusterId, "test"), exception::ClusterDeleted, "Blob creation before committing DeleteCluster should throw");
  Transaction::Commit(session);

  // Now in the next transaction the previously read blob can also not be accessed
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(0, clusterId, 0), exception::BlobDoesNotExist, "Reading after committing DeleteCluster should throw");
  REQUIRE_THROWS_AS_MESSAGE(mainDb->ReadString(0, clusterId, 0), exception::BlobDoesNotExist, "Reading in main session after committing DeleteCluster should throw");
}


// This test case will test the use case of first creating a new blob in a cluster and then deleting the cluster in the same transaction
TEST_CASE("Create Blob then delete the cluster") {
  // Prepare test
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", "mem:createBlobDeleteCluster"));
  auto clusterId = db->CreateCluster(0);
  Transaction::Commit(session);

  // Create a blob inside the cluster and then delete the cluster itself
  auto blobId = db->CreateString(0, clusterId, "new");
  REQUIRE(blobId == 1);
  db->DeleteCluster(0, clusterId);

  // Now reading any string even the created should throw
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(0, clusterId, 0), exception::ClusterDeleted, "Reading the default blob after calling DeleteCluster should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(0, clusterId, blobId), exception::ClusterDeleted, "Reading the created blob after calling DeleteCluster should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(0, clusterId, "xxx"), exception::ClusterDeleted, "Creating a new blob after DeleteCluster should fail");

  // Now commit the transaction
  Transaction::Commit(session);

  // Now reading any of the two blobs should throw an error as well as attempting to create a new blob in the deleted cluster
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(0, clusterId, 0), exception::BlobDoesNotExist, "Reading the default blob of the deleted cluster after commit should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(0, clusterId, blobId), exception::BlobDoesNotExist, "Reading the created blob of the deleted cluster after commit should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(0, clusterId, "test"), exception::BlobDoesNotExist, "Creating a new blob in the previously deleted cluster should fail");
}


// This test case simply checks that the same session can access the cluster if it aborted the deletion in a previous transaction
TEST_CASE("Accessing a cluster form the same session after aborting its deletion") {
  // Prepare test
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", "mem:deleteClusterAbortAccess"));
  auto clusterId = db->CreateCluster(0);
  auto blobId = db->CreateString(0, clusterId, "new");
  REQUIRE(blobId == 1);
  Transaction::Commit(session);

  // Delete the cluster in a new transaction and abort it
  db->DeleteCluster(0, clusterId);
  Transaction::Abort(session);

  // Now we should be able to read the previously created blobs from that cluster and create a new blob
  REQUIRE(db->ReadString(0, clusterId, 0) == "");
  REQUIRE(db->ReadString(0, clusterId, blobId) == "new");
  REQUIRE(db->CreateString(0, clusterId, "test") == 2);
}
