#include "pch.hpp"

using namespace blobs;


namespace {

// We want to use the same db for all these tests
Database* testDb() {
  static auto db = Database::Open("localhost", "create_cluster.db");
  return db;
}

}


SCENARIO("Create cluster") {
  GIVEN("An empty database") {
    auto db = testDb();

    THEN("Creating a new cluster in 0 should return id 1") {
      cluster_id id;
      REQUIRE_NOTHROW(id = db->CreateCluster(0));
      REQUIRE(id == 1);
    }

    THEN("Creating another cluster in 0 should return 2") {
      cluster_id id;
      REQUIRE_NOTHROW(id = db->CreateCluster(0));
      REQUIRE(id == 2);
    }

    THEN("Creating a cluster in 1 should fail") {
      REQUIRE_THROWS_AS(db->CreateCluster(1), Exception);
    }

    THEN("Creating a new cluster in 0 after aborting the transaction should again return 1") {
      Transaction::Abort();
      cluster_id id;
      REQUIRE_NOTHROW(id = db->CreateCluster(0));
      REQUIRE(id == 1);
    }

    THEN("Reading blob 0,1,0 should return \"\"") {
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(0, 1, 0));
      REQUIRE(data == "");
    }

    THEN("Reading blob 0,1,2 should fail") {
      REQUIRE_THROWS_AS(db->ReadBlob(0, 1, 2), exception::BlobDoesNotExist);
    }

    THEN("After committing the transaction creating a new cluster in 0 should return 2") {
      Transaction::Commit();
      cluster_id id;
      REQUIRE_NOTHROW(id = db->CreateCluster(0));
      REQUIRE(id == 2);
    }

    THEN("Creating a new blob in 0,1 should return 1") {
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(0,1, "test"));
      REQUIRE(id == 1);
    }

    THEN("Creating a new blob in 0,2 should also return 1") { // no global blob id counter
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(0, 2, "test"));
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
}