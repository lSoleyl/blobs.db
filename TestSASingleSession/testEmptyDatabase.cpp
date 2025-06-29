#include "pch.hpp"

using namespace blobs;


namespace {

// We want to use the same db for all these tests
Database* emptyDb() {
  static auto db = Database::Open("localhost", "empty.db");
  return db;
}


}




SCENARIO("Empty database") {
  GIVEN("An empty database") {
    auto db = emptyDb();
    
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


  GIVEN("An empty database for blob creation") {
    auto db = emptyDb();

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






}