#include "pch.hpp"

using namespace blobs;


namespace {

// We want to use the same db for all these tests
Database* testDb() {
  static auto db = Database::Open("localhost", "create_blob.db");
  return db;
}

}


SCENARIO("Create blob") {
  GIVEN("An empty database") {
    auto db = testDb();

    THEN("Creating a new blob in 0,0 should return id 1") {
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(0, 0, "new"));
      REQUIRE(id == 1);
    }

    THEN("Creating another blob in 0,0 should return 2") {
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(0, 0, "new2"));
      REQUIRE(id == 2);
    }

    THEN("Creating a blob in 0,1 should fail") {
      REQUIRE_THROWS_AS(db->CreateString(0, 1, "dummy"), Exception);
    }

    THEN("Creating a blob in 1,0 should fail") {
      REQUIRE_THROWS_AS(db->CreateString(1, 0, "dummy"), Exception);
    }

    THEN("Creating a new blob in 0,0 after aborting the transaction should again return 1") {
      Transaction::Abort();
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(0, 0, "new1"));
      REQUIRE(id == 1);
    }

    THEN("Reading blob 0,0,1 should return \"new1\"") {
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(0, 0, 1));
      REQUIRE(data == "new1");
    }

    THEN("Reading blob 0,0,2 should fail") {
      REQUIRE_THROWS_AS(db->ReadBlob(0, 0, 2), exception::BlobDoesNotExist);
    }

    THEN("After committing the transaction creating a new blob in 0,0 should return 2") {
      Transaction::Commit();
      blob_id id;
      REQUIRE_NOTHROW(id = db->CreateString(0, 0, "new2"));
      REQUIRE(id == 2);
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