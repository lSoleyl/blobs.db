#include "pch.hpp"

using namespace blobs;


namespace {

// We want to use the same db for all these tests
Database* testDb() {
  static auto db = Database::Open("localhost", "empty.db");
  return db;
}


}




SCENARIO("Empty database") {
  GIVEN("An empty database") {
    auto db = testDb();
    
    THEN("The blob 0,0,0 should be an existing, empty blob") {
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(0, 0, 0));
      REQUIRE(data.empty());
    }

    THEN("Reading blob 0,0,1 should fail") {
      REQUIRE_THROWS_AS(db->ReadBlob(0, 0, 1), exception::BlobDoesNotExist);
    }

    THEN("Reading blob 0,1,0 should fail") {
      REQUIRE_THROWS_AS(db->ReadBlob(0, 1, 0), exception::BlobDoesNotExist);
    }

    THEN("Reading blob 1,0,0 should fail") {
      REQUIRE_THROWS_AS(db->ReadBlob(1, 0, 0), exception::BlobDoesNotExist);
    }

    THEN("Writing \"hello\" to 0,0,0 shouldn't fail") {
      REQUIRE_NOTHROW(db->WriteString(0, 0, 0, "hello"));
    }

    THEN("Reading it back should return \"hello\"") {
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(0, 0, 0));
      REQUIRE(data == "hello");
    }

    THEN("Reading it back after commiting the transaction should still return \"hello\"") {
      Transaction::Commit();
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(0, 0, 0));
      REQUIRE(data == "hello");
    }

    THEN("Writing \"bye\" to 0,0,0 should return \"bye\"") {
      REQUIRE_NOTHROW(db->WriteString(0, 0, 0, "bye"));
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(0, 0, 0));
      REQUIRE(data == "bye");
    }

    THEN("Aborting the Transaction shouldn't fail") {
      REQUIRE_NOTHROW(Transaction::Abort());
    }

    THEN("Reading 0,0,0 after the abort should return \"hello\" again") {
      std::string data;
      REQUIRE_NOTHROW(data = db->ReadString(0, 0, 0));
      REQUIRE(data == "hello");
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