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
TEST_CASE("GetAllSegments tests with default segment") {
  auto session = Session::Create();
  auto dbName = "mem:testGetAllSegmentsDefault";
  database_ptr db(Database::Open(session, "localhost", dbName));

  auto segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == std::vector<segment_id>{0}, "Expected other segment list for initial state of default segment");

  // Now delete and create segments in the same transaction
  db->DeleteSegment(0); // default segment
  segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == std::vector<segment_id>{}, "Expected no segments after deleting the default segment");
  REQUIRE(db->CreateSegment() == 1);
  REQUIRE(db->CreateSegment() == 2);

  segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == (std::vector<segment_id>{1, 2}), "Expected 1,2");

  // Now test whether two ranges of segment ids are handled correctly
  REQUIRE(db->CreateSegment() == 3);
  REQUIRE(db->CreateSegment() == 4);
  REQUIRE(db->CreateSegment() == 5);

  segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == (std::vector<segment_id>{1, 2, 3, 4, 5}), "Expected 1,2,3,4,5");

  // Now delete the segment in the middle to create two ranges
  db->DeleteSegment(3);
  segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == (std::vector<segment_id>{1, 2, 4, 5}), "Expected 1,2,4,5");

  // Now abort and verify that the list has been reverted
  Transaction::Abort(session);
  segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == (std::vector<segment_id>{0}), "Expected 0 after aborting the transaction");

  // Create segments and delete one
  REQUIRE(db->CreateSegment() == 1);
  REQUIRE(db->CreateSegment() == 2);
  REQUIRE(db->CreateSegment() == 3);
  REQUIRE(db->CreateSegment() == 4);
  db->DeleteSegment(2);

  // This time commit the changes
  Transaction::Commit(session);

  segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == (std::vector<segment_id>{0, 1, 3, 4}), "Expected 0,1,3,4 in second transaction");
}

// Here we ensure that the list will be retrieved in the correct state even if we first perform modifications that affect the list
// and then request it from the server
TEST_CASE("Delete and create segment then GetAllSegments") {
  auto session = Session::Create();
  auto dbName = "mem:testModifyListThenGetAllSegments";
  database_ptr db(Database::Open(session, "localhost", dbName));

  db->DeleteSegment(0);
  REQUIRE(db->CreateSegment() == 1);
  REQUIRE(db->CreateSegment() == 2);
  REQUIRE(db->CreateSegment() == 3);
  REQUIRE(db->CreateSegment() == 4);
  db->DeleteSegment(3); // this segment hasn't been committed yet


  auto segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == (std::vector<segment_id>{1, 2, 4}), "Expected 1,2,4 when requested after modifications");
  Transaction::Commit(session);

  segments = intoVector(db->GetAllSegments());
  REQUIRE_MESSAGE(segments == (std::vector<segment_id>{1, 2, 4}), "Expected 1,2,4 when requested after committing the first transaction");
}


// This test ensures correct locking semantics for the segment id list in respect to other operations
TEST_CASE("Querying all segments should block segment creation/deletion, but not cluster creation") {
  const auto dbName = "mem:testGetAllSegmentsLockingSemantics";
  database_ptr db(Database::Open("localhost", dbName));
  REQUIRE(db->CreateSegment() == 1); // prepare database
  REQUIRE(db->CreateCluster(1) == 1);
  Transaction::Commit();

  std::atomic<std::chrono::high_resolution_clock::time_point> firstClientCommit, clusterCreated, clusterDeleted, segmentDeleted, segmentCreated, blobWritten, blobCreated;

  parallel::sync_point syncPoint(7);
  parallel::run({
    // The first client will simply query the segment list
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      auto range = db->GetAllSegments();
      syncPoint.wait();

      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to measure the lock
      firstClientCommit = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The second client will attempt to create a cluster in segment 1
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateCluster(1) == 2);
      clusterCreated = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The third client will attempt to delete the default cluster in the default segment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->DeleteCluster(1, 0);
      clusterDeleted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The fourth client will attempt to delete the default segment
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->DeleteSegment(0);
      segmentDeleted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The fifth client will simply create a new segment, which should be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateSegment() == 2);
      segmentCreated = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
    },

    // The sixth client will simply write the default blob of segment 1, cluster 1, which should not be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      db->WriteString(1, 1, 0, "xxx");
      blobWritten = std::chrono::high_resolution_clock::now();
      Transaction::Commit();
    },

    // The seventh client will create a new blob in segment 1, cluster 1, which should also not be blocked
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      syncPoint.wait();
      CHECK(db->CreateString(1, 1, "xxx") == 1);
      blobWritten = std::chrono::high_resolution_clock::now();
      Transaction::Commit();
    }
    });


  REQUIRE_MESSAGE(firstClientCommit.load() < segmentDeleted.load(), "Reading the segment list should block segment deletion");
  REQUIRE_MESSAGE(firstClientCommit.load() < segmentCreated.load(), "Reading the segment list should block segment creation");

  REQUIRE_MESSAGE(clusterDeleted.load() < firstClientCommit.load(), "Deleting a cluster should not be blocked by reading the segment list");
  REQUIRE_MESSAGE(clusterCreated.load() < firstClientCommit.load(), "Creating a cluster should not be blocked by reading the segment list");
  REQUIRE_MESSAGE(blobWritten.load() < firstClientCommit.load(), "Writing a blob inside the segment should not be blocked by reading the cluster list");
  REQUIRE_MESSAGE(blobCreated.load() < firstClientCommit.load(), "Creating a blob inside the segment should not be blocked by reading the cluster list");
}

// Tests for operations that should block querying the segment id list
TEST_CASE("Querying segment list") {
  std::atomic<std::chrono::high_resolution_clock::time_point> readListCompleted, blockingTransactionCompleted;
  parallel::sync_point syncPoint(2);

  SUBCASE("should be blocked by another client creating a segment") {
    const auto dbName = "mem:GetAllSegmentsLockingSemantics2";

    parallel::run({
      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        CHECK(db->CreateSegment() == 1);
        syncPoint.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect the blocking
        blockingTransactionCompleted = std::chrono::high_resolution_clock::now();
        Transaction::Commit(session);
      },

      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        syncPoint.wait();
        auto segments = intoVector(db->GetAllSegments());
        readListCompleted = std::chrono::high_resolution_clock::now();
        CHECK(segments == std::vector<cluster_id>{0, 1});
      }
      });

    REQUIRE_MESSAGE(blockingTransactionCompleted.load() < readListCompleted.load(), "GetAllSegments() should be blocked by segment creation");
  }


  SUBCASE("should be blocked by another client deleting a segment") {
    const auto dbName = "mem:GetAllSegmentsLockingSemantics2";

    parallel::run({
      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        db->DeleteSegment(0);
        syncPoint.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // just long enough to detect the blocking
        blockingTransactionCompleted = std::chrono::high_resolution_clock::now();
        Transaction::Commit(session);
      },

      [&]() {
        auto session = Session::Create();
        database_ptr db(Database::Open(session, "localhost", dbName));
        syncPoint.wait();
        auto segments = intoVector(db->GetAllSegments());
        readListCompleted = std::chrono::high_resolution_clock::now();
        CHECK(segments == std::vector<cluster_id>{});
      }
      });

    REQUIRE_MESSAGE(blockingTransactionCompleted.load() < readListCompleted.load(), "GetAllSegments() should be blocked by segment deletion");
  }
}
