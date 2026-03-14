#include "pch.hpp"


using namespace blobs;

namespace {
  template<typename T>
  std::vector<T> intoVector(blobs::Range<T>&& range) {
    return std::vector<T>(range.begin(), range.end());
  }
}

// This test will ensure that the list can be correctly queried and is correctly adjusted to the modifications
// performed during a transaction and that it is correctly read across transaction.
TEST_CASE("GetAllClusters tests with default segment") {
  auto session = Session::Create();
  auto dbName = "mem:testGetAllClustersDefault";
  database_ptr db(Database::Open(session, "localhost", dbName));

  auto clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == std::vector<cluster_id>{0}, "Expected other cluster list for initial state of default segment");

  // Now delete and create clusters in the same transaction
  db->DeleteCluster(0, 0); // default cluster
  clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == std::vector<cluster_id>{}, "Expected no clusters after deleting the default cluster");
  REQUIRE(db->CreateCluster(0) == 1);
  REQUIRE(db->CreateCluster(0) == 2);

  clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == (std::vector<cluster_id>{1, 2}), "Expected 1,2");

  // Now test whether two ranges of cluster ids are handled correctly
  REQUIRE(db->CreateCluster(0) == 3);
  REQUIRE(db->CreateCluster(0) == 4);
  REQUIRE(db->CreateCluster(0) == 5);

  clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == (std::vector<cluster_id>{1, 2, 3, 4, 5}), "Expected 1,2,3,4,5");

  // Now delete the cluster in the middle to create two ranges
  db->DeleteCluster(0, 3);
  clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == (std::vector<cluster_id>{1, 2, 4, 5}), "Expected 1,2,4,5");

  // Now abort and verify that the list has been reverted
  Transaction::Abort(session);
  clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == (std::vector<cluster_id>{0}), "Expected 0 after aborting the transaction");

  // Create blobs and delete one
  REQUIRE(db->CreateCluster(0) == 1);
  REQUIRE(db->CreateCluster(0) == 2);
  REQUIRE(db->CreateCluster(0) == 3);
  REQUIRE(db->CreateCluster(0) == 4);
  db->DeleteCluster(0, 2);

  // This time commit the changes
  Transaction::Commit(session); 

  clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == (std::vector<cluster_id>{0,1,3,4}), "Expected 0,1,3,4 in second transaction");
}

// Here we ensure that the list will be retrieved in the correct state even if we first perform modifications that affect the list
// and then request it from the server
TEST_CASE("Delete and create cluster then GetAllClusters") {
  auto session = Session::Create();
  auto dbName = "mem:testModifyListThenGetAllClusters";
  database_ptr db(Database::Open(session, "localhost", dbName));

  db->DeleteCluster(0, 0);
  REQUIRE(db->CreateCluster(0) == 1);
  REQUIRE(db->CreateCluster(0) == 2);
  REQUIRE(db->CreateCluster(0) == 3);
  REQUIRE(db->CreateCluster(0) == 4);
  db->DeleteCluster(0, 3); // this cluster hasn't been committed yet


  auto clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == (std::vector<cluster_id>{1, 2, 4}), "Expected 1,2,4 when requested after modifications");
  Transaction::Commit(session);

  clusters = intoVector(db->GetAllClusters(0));
  REQUIRE_MESSAGE(clusters == (std::vector<cluster_id>{1, 2, 4}), "Expected 1,2,4 when requested after committing the first transaction");
}


// This test ensures correct locking semantics for the cluster id list in respect to other operations
TEST_CASE("Querying all clusters should block cluster creation/deletion and segment deletion, but not segment creation") {
  const auto dbName = "mem:testGetAllClustersLockingSemantics";
  database_ptr db(Database::Open("localhost", dbName));
  REQUIRE(db->CreateCluster(0) == 1); // prepare database
  Transaction::Commit();

  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientCommit, clusterCreated, clusterDeleted, segmentDeleted, segmentCreated, blobWritten, blobCreated;

  parallel::sync_point syncPoint(7);
  parallel::run({
    // The first client will simply query the cluster list of the default segment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      auto range = db->GetAllClusters(0);
      syncPoint.wait();

      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the lock
      firstClientCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will attempt to create a cluster in the default segment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateCluster(0) == 2);
      clusterCreated = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The third client will attempt to delete a cluster in the default segment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->DeleteCluster(0, 0);
      clusterDeleted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },
    
    // The fourth client will attempt to delete the segment, but will abort that transaction to not impact clients two and three
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->DeleteSegment(0);
      segmentDeleted = std::chrono::high_resolution_clock::now();
      Transaction::Abort(session);
    },

    // The fifth client will simply create a new segment, which should not be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateSegment() == 1);
      segmentCreated = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The sixth client will simply write the default blob of cluster 1, whcih should not be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->WriteString(0, 1, 0, "xxx");
      blobWritten = std::chrono::high_resolution_clock::now();
      Transaction::Commit();
    },

    // The seventh client will create a new blob in cluster 1, which should also not be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateString(0, 1, "xxx") == 1);
      blobWritten = std::chrono::high_resolution_clock::now();
      Transaction::Commit();
    }
  });

  
  REQUIRE_MESSAGE(firstClientCommit.load() < clusterDeleted.load(), "Reading the cluster list should block cluster deletion in that segment");
  REQUIRE_MESSAGE(firstClientCommit.load() < clusterCreated.load(), "Reading the cluster list should block cluster creation in that segment");
  REQUIRE_MESSAGE(firstClientCommit.load() < segmentDeleted.load(), "Reading the cluster list should block the segments's deletion");

  REQUIRE_MESSAGE(segmentCreated.load() < firstClientCommit.load(), "Creating a new segment should not be blocked by reading the cluster list of another segment");
  REQUIRE_MESSAGE(blobWritten.load() < firstClientCommit.load(), "Writing a blob inside the segment should not be blocked by reading the cluster list");
  REQUIRE_MESSAGE(blobCreated.load() < firstClientCommit.load(), "Creating a blob inside the segment should not be blocked by reading the cluster list");
}

// Tests for operations that should block querying the cluster id list
TEST_CASE("Querying cluster list") {
  std::atomic<std::chrono::high_resolution_clock::time_point> readListCompleted, blockingTransactionCompleted;
  parallel::sync_point syncPoint(2);

  SUBCASE("should be blocked by another client creating a cluster in the same segment") {
    const auto dbName = "mem:GetAllClustersLockingSemantics2";

    parallel::run({
      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        CHECK(db->CreateCluster(0) == 1);
        syncPoint.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect the blocking
        blockingTransactionCompleted = std::chrono::high_resolution_clock::now();
        Transaction::Commit(session);
      },

      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        syncPoint.wait();
        auto clusters = intoVector(db->GetAllClusters(0));
        readListCompleted = std::chrono::high_resolution_clock::now();
        CHECK(clusters == std::vector<cluster_id>{0, 1});
      }
    });

    REQUIRE_MESSAGE(blockingTransactionCompleted.load() < readListCompleted.load(), "GetAllClusters() should be blocked by cluster creation in the same segment");
  }


  SUBCASE("should be blocked by another client deleting a cluster in the same segment") {
    const auto dbName = "mem:GetAllClustersLockingSemantics2";

    parallel::run({
      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        db->DeleteCluster(0, 0);
        syncPoint.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect the blocking
        blockingTransactionCompleted = std::chrono::high_resolution_clock::now();
        Transaction::Commit(session);
      },

      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        syncPoint.wait();
        auto clusters = intoVector(db->GetAllClusters(0));
        readListCompleted = std::chrono::high_resolution_clock::now();
        CHECK(clusters == std::vector<cluster_id>{});
      }
      });

    REQUIRE_MESSAGE(blockingTransactionCompleted.load() < readListCompleted.load(), "GetAllClusters() should be blocked by cluster deletion in the same segment");
  }
}





// This test will ensure that segment creation will implcitly grant a lock (transferred as sticky lock into the next transaction) on the 
// ClusterListId of the created segment.
TEST_CASE("Create segment sticky lock on ClusterListId") {
  const auto dbName = "mem:testCreateSegmentLockClusterList";
  parallel::sync_point syncPoint(2);

  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientCommit, secondClientReadClusters;
  parallel::run({
    // The first client will create a segment, commit and then start a new transaction (thus holding the lock on the cluster list)
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

    // The second client will wait for the first client to finish preparations and then attempt to simply read the cluster list of the created segment
    // This should be blocked for as long as the first client doesn't commit his transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      auto clusters = intoVector(db->GetAllClusters(1));
      secondClientReadClusters = std::chrono::high_resolution_clock::now();
      CHECK(clusters == std::vector<cluster_id>{0}); // Only the default cluster should exist in the newly created segment
    }
  });

  REQUIRE_MESSAGE(firstClientCommit.load() < secondClientReadClusters.load(), "The second client's read should have been blocked until the first client completes its transaction");
}




TEST_CASE("GetAllClusters after CreateSegment in same transaction") {
  const auto dbName = "mem:testGetAllClustersAfterCreateSegment";
  auto session = Session::Create();

  database_ptr db(Database::Open(session, "localhost", dbName));
  auto segment = db->CreateSegment();
  REQUIRE_MESSAGE(segment == 1, "Newly created segment has wrong id");

  blobs::Range<cluster_id> clusters;
  REQUIRE_NOTHROW(clusters = db->GetAllClusters(segment));
  REQUIRE_MESSAGE(clusters.size() == 1, "The returned cluster range should contain one cluster");
  REQUIRE_MESSAGE(*clusters.begin() == 0, "The new segment should only consist of the cluster 0");
}


//TODO: we could also test that segment creation implicitly grants locks on the blob id list of the default cluster in that segment
