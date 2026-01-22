#include "pch.hpp"

using namespace blobs;


// This test will ensure that a client, which attempts to delete a segment will not be able to do so for 
// as long as another client still holds a lock to any blob inside the segment
TEST_CASE("Locking semantics during segment deletion") {
  auto dbName = "mem:lockingDuringSegmentDeletion";
  database_ptr db(Database::Open("localhost", dbName));
  segment_id createdSegment = db->CreateSegment();
  cluster_id createdCluster = db->CreateCluster(createdSegment);
  db->WriteString(createdSegment, createdCluster, 0, "created");
  Transaction::Commit();

  parallel::sync_point beforeDelete(2);
  parallel::sync_point afterDelete(2);


  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientSegmentDeleted, firstClientBeforeCommit;
  std::atomic<std::chrono::high_resolution_clock::time_point> secondClientBeforeCommit;
  std::atomic<std::chrono::high_resolution_clock::time_point> thirdClientBlobRead;


  parallel::run({
    // The main thread will simply attempt to delete the created cluster
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));

      beforeDelete.wait();
      db->DeleteSegment(createdSegment);
      firstClientSegmentDeleted = std::chrono::high_resolution_clock::now();

      CHECK_THROWS_AS_MESSAGE(db->WriteString(createdSegment, createdCluster, 0, "test"), exception::SegmentDeleted, "Updating a blob after deleting its segment should throw");
      CHECK_THROWS_AS_MESSAGE(db->WriteString(createdSegment, 0, 0, "test"), exception::SegmentDeleted, "Updating a blob after deleting its segment should throw");

      afterDelete.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the blocking of the third thread
      firstClientBeforeCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second thread will read the only blob inside the segment to delete and then wait for a moment to block the main thread
    // from proceeding with the segments's deletion to ensure that a lock inside the segment will prevent the segment from being deleted
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK(db->ReadString(createdSegment, createdCluster, 0) == "created");
      beforeDelete.wait(); // sync with main thread
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the blocking of the main thread
      secondClientBeforeCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The third thread will attempt to read a blob inside the segment AFTER the main thread has called DeleteSegment(), which should block 
    // until the main thread finishes its transaction and then throw an exception due to deleted segment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      afterDelete.wait();
      // The following read should now block until the main thread completes the transaction
      CHECK_THROWS_AS_MESSAGE(db->ReadString(createdSegment, createdCluster, 0), exception::BlobDoesNotExist, "Expected exception to be thrown when reading from a deleted segment");
      CHECK_THROWS_AS_MESSAGE(db->ReadString(createdSegment, 0, 0), exception::BlobDoesNotExist, "Expected exception to be thrown when reading from a deleted segment");
      thirdClientBlobRead = std::chrono::high_resolution_clock::now();
    }
  });

  REQUIRE_MESSAGE(secondClientBeforeCommit.load() < firstClientSegmentDeleted.load(), "The main thread should have been blocked from deleting the segment by the second thread");
  REQUIRE_MESSAGE(firstClientBeforeCommit.load() < thirdClientBlobRead.load(), "The third thread has to wait for the main thread to complete");
}


// This test will ensure that another client will be blocked while main client attempts to delete a segment, but 
// will be able to access the segment normally as soon as the main client aborts its transaction
TEST_CASE("Locking and access semantics during segment deletion abort in other client") {
  auto dbName = "mem:lockingDuringSegmentDeletionAbort";
  database_ptr db(Database::Open("localhost", dbName));
  segment_id createdSegment = db->CreateSegment();
  cluster_id createdCluster = db->CreateCluster(createdSegment);
  db->WriteString(createdSegment, createdCluster, 0, "created");
  Transaction::Commit();

  parallel::sync_point afterDelete(2);


  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientBeforeAbort;
  std::atomic<std::chrono::high_resolution_clock::time_point> secondClientBlobRead;


  parallel::run({
    // The main thread will simply attempt to delete the created segment and Abort the transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      db->DeleteSegment(createdSegment);
      afterDelete.wait();

      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the blocking of the third thread
      firstClientBeforeAbort = std::chrono::high_resolution_clock::now();
      Transaction::Abort(session);
    },

    // The second thread will attempt to read a blob in the segment that is marked for deletion and also attempt to create a new blob and a new cluster
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      afterDelete.wait(); // sync with main thread

      CHECK(db->ReadString(createdSegment, createdCluster, 0) == "created");
      secondClientBlobRead = std::chrono::high_resolution_clock::now();
      CHECK_NOTHROW_MESSAGE(db->CreateString(createdSegment, createdCluster, "new"), "Creating a blob in the segment should be allowed if the transaction was aborted");
      CHECK_NOTHROW_MESSAGE(db->CreateCluster(createdSegment), "Creating a new cluster inside the segment should be allowed if the transaction was aborted");
      Transaction::Commit(session);
    }
  });

  REQUIRE_MESSAGE(firstClientBeforeAbort.load() < secondClientBlobRead.load(), "The main thread should have blocked the second client from reading a blob in the segment");
  REQUIRE(db->ReadString(createdSegment, createdCluster, 0) == "created");
  REQUIRE(db->ReadString(createdSegment, createdCluster, 1) == "new");
  REQUIRE(db->ReadString(createdSegment, createdCluster + 1, 0) == ""); // should also not throw!
}


// This single session test case checks three things:
//  1. the client attempting to delete the segment can do so even if they already accessed the segment before
//  2. the segment cannot be read from / written to after the call to DeleteSegment()
//  3. a blob inside that segment can also not be accessed after the transaction commits
TEST_CASE("Deleting a segment when already holding locks into it") {
  auto dbName = "mem:deleteSegmentCommitBlobAccess";
  // Prepare the database in the main session
  database_ptr mainDb(Database::Open("localhost", dbName));
  segment_id segmentId = mainDb->CreateSegment();
  cluster_id clusterId = mainDb->CreateCluster(segmentId);
  mainDb->WriteString(segmentId, clusterId, 0, "init");
  Transaction::Commit();

  // Now run the test in a separate session (to avoid affecting the main session)
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", dbName));
  // Read a blob in the cluster
  REQUIRE_MESSAGE(db->ReadString(segmentId, clusterId, 0) == "init", "Initial blob value is wrong");

  // And in the same transaction delete the segment now the above read lock should not hinder us from deleting the cluster
  db->DeleteSegment(segmentId);

  // Reading the same blob and creating new blobs and clusters should both throw (should be handled in the client)
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, 0), exception::SegmentDeleted, "Reading before committing DeleteSegment should throw");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(segmentId, clusterId, "test"), exception::SegmentDeleted, "Blob creation before committing DeleteSegment should throw");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateCluster(segmentId), exception::SegmentDeleted, "Cluster creation before committing DeleteSegment should throw");
  Transaction::Commit(session);

  // Now in the next transaction the previously read blob can also not be accessed
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, 0), exception::BlobDoesNotExist, "Reading after committing DeleteSegment should throw");
  REQUIRE_THROWS_AS_MESSAGE(mainDb->ReadString(segmentId, clusterId, 0), exception::BlobDoesNotExist, "Reading in main session after committing DeleteSegment should throw");
}


// This test case will test the use case of first creating a new blob in a segment and then deleting the segment in the same transaction
TEST_CASE("Create blob then delete the segment") {
  // Prepare test
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", "mem:createBlobDeleteSegment"));
  auto segmentId = db->CreateSegment();
  auto clusterId = db->CreateCluster(segmentId);
  Transaction::Commit(session);

  // Create a blob inside the segment and then delete the segment itself
  auto blobId = db->CreateString(segmentId, clusterId, "new");
  REQUIRE(blobId == 1);
  db->DeleteSegment(segmentId);

  // Now reading any string even the created should throw
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, 0), exception::SegmentDeleted, "Reading the default blob after calling DeleteSegment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, blobId), exception::SegmentDeleted, "Reading the created blob after calling DeleteSegment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(segmentId, clusterId, "xxx"), exception::SegmentDeleted, "Creating a new blob after DeleteSegment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateCluster(segmentId), exception::SegmentDeleted, "Creating a new cluster after DeleteSegment should fail");

  // Now commit the transaction
  Transaction::Commit(session);

  // Now reading any of the two blobs should throw an error as well as attempting to create a new blob or cluster in the deleted segment
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, 0, 0), exception::BlobDoesNotExist, "Reading the default blob of the default cluster of the deleted segment after commit should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, 0), exception::BlobDoesNotExist, "Reading the default blob of the created cluster of the deleted segment after commit should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, blobId), exception::BlobDoesNotExist, "Reading the created blob of the deleted segment after commit should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(segmentId, clusterId, "test"), exception::BlobDoesNotExist, "Creating a new blob in the previously deleted segment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateCluster(segmentId), exception::BlobDoesNotExist, "Creating a new cluster in the previously deleted segment should fail");
}


// This test case will test the use case of first creating a new cluster in a segment and then deleting the segment in the same transaction
TEST_CASE("Create cluster then delete the segment") {
  // Prepare test
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", "mem:createClusterDeleteSegment"));
  auto segmentId = db->CreateSegment();
  Transaction::Commit(session);

  // Create a blob inside the segment and then delete the segment itself
  auto clusterId = db->CreateCluster(segmentId);
  REQUIRE(clusterId == 1);
  db->DeleteSegment(segmentId);

  // Now reading/writing any blob in the segment should throw
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, 0, 0), exception::SegmentDeleted, "Reading the default clusters default blob after calling DeleteSegment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, 0), exception::SegmentDeleted, "Reading the default blob after calling DeleteSegment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(segmentId, clusterId, "xxx"), exception::SegmentDeleted, "Creating a new blob after DeleteSegment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateCluster(segmentId), exception::SegmentDeleted, "Creating a new cluster after DeleteSegment should fail");

  // Now commit the transaction
  Transaction::Commit(session);

  // Now reading any blob should throw an error as well as attempting to create a new blob or cluster in the deleted segment
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, 0, 0), exception::BlobDoesNotExist, "Reading the default blob of the default cluster of the deleted segment after commit should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->ReadString(segmentId, clusterId, 0), exception::BlobDoesNotExist, "Reading the default blob of the created cluster of the deleted segment after commit should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateString(segmentId, clusterId, "test"), exception::BlobDoesNotExist, "Creating a new blob in the previously deleted segment should fail");
  REQUIRE_THROWS_AS_MESSAGE(db->CreateCluster(segmentId), exception::BlobDoesNotExist, "Creating a new cluster in the previously deleted segment should fail");
}



// This test case simply checks that the same session can access the segment if it aborted the deletion in a previous transaction
TEST_CASE("Accessing a segment form the same session after aborting its deletion") {
  // Prepare test
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", "mem:deleteSegmentAbortAccess"));
  auto segmentId = db->CreateSegment();
  auto clusterId = db->CreateCluster(segmentId);
  auto blobId = db->CreateString(segmentId, clusterId, "new");
  REQUIRE(blobId == 1);
  Transaction::Commit(session);

  // Delete the segment in a new transaction and abort it
  db->DeleteSegment(segmentId);
  Transaction::Abort(session);

  // Now we should be able to read the previously created blobs from that cluster and create a new blob and cluster
  REQUIRE(db->ReadString(segmentId, clusterId, 0) == "");
  REQUIRE(db->ReadString(segmentId, clusterId, blobId) == "new");
  REQUIRE(db->CreateString(segmentId, clusterId, "test") == 2);
  REQUIRE(db->CreateCluster(segmentId) == clusterId + 1);
}


TEST_CASE("Deleting a non existent segment") {
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", "mem:deleteNotExistingSegment"));
  REQUIRE_THROWS_AS_MESSAGE(db->DeleteSegment(1), exception::SegmentDoesNotExist, "Expected exception to be thrown when attempting to delete segment that doesn't exist");
}

