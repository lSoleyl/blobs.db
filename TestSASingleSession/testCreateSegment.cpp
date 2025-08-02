#include "pch.hpp"

using namespace blobs;


namespace {

// We want to use the same db for all these tests
Database* testDb() {
  static auto db = Database::Open("localhost", "mem:create_segment.db");
  return db;
}

}


SCENARIO("Create segment") {
  GIVEN("An empty database") {
    auto db = testDb();

    THEN("Creating a new segment should return id 1") {
      segment_id id;
      REQUIRE_NOTHROW(id = db->CreateSegment());
      REQUIRE(id == 1);
    }

    THEN("Creating another segment should return 2") {
      segment_id id;
      REQUIRE_NOTHROW(id = db->CreateSegment());
      REQUIRE(id == 2);
    }

    THEN("Creating a new segment after aborting the transaction should again return 1") {
      Transaction::Abort();
      segment_id id;
      REQUIRE_NOTHROW(id = db->CreateSegment());
      REQUIRE(id == 1);
    }

    THEN("Reading blob 1,0,0 should return \"\"") {
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(1, 0, 0));
      REQUIRE(data == "");
    }

    THEN("Reading blob 1,1,0 should fail") {
      REQUIRE_THROWS_AS(db->ReadBlob(1, 1, 0), exception::BlobDoesNotExist);
    }

    THEN("Reading blob 2,0,0 should fail") {
      REQUIRE_THROWS_AS(db->ReadBlob(2, 0, 0), exception::BlobDoesNotExist);
    }

    THEN("After committing the transaction creating a new segment should return 2") {
      Transaction::Commit();
      segment_id id;
      REQUIRE_NOTHROW(id = db->CreateSegment());
      REQUIRE(id == 2);
    }

    THEN("Creating a new cluster in 1 should return 1") { // the already committed segment
      cluster_id id;
      REQUIRE_NOTHROW(id = db->CreateCluster(1));
      REQUIRE(id == 1);
    }

    THEN("Creating a new cluster in 2 should also return 1") { // the uncommitted segment
      cluster_id id;
      REQUIRE_NOTHROW(id = db->CreateCluster(2));
      REQUIRE(id == 1);
    }


    THEN("Creating a new blob in 1,0 should return 1") {
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(1, 0, "test"));
      REQUIRE(id == 1);
    }

    THEN("Creating a new blob in 2,1 should also return 1") { // in uncommitted cluster in uncommitted segment
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(2, 1, "test"));
      REQUIRE(id == 1);
    }
  }

  GIVEN("The test database") {
    auto db = testDb();
    THEN("Closing it while the transaction is still open should fail") {
      REQUIRE_THROWS_AS(db->Close(), Exception);
    }

    THEN("Closing it after aborting the transaction should not fail") {
      REQUIRE_NOTHROW(Transaction::Abort());
      REQUIRE_NOTHROW(db->Close());
    }
  }

  FIXME("Make sure, DeleteCluster/DeleteBlob behave correctly for Clusters created in the same transaction and for clusters created in different transactions");
}