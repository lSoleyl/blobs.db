#include "pch.hpp"

using namespace blobs;


SCENARIO("Opening multiple databases") {
  GIVEN("No database is opened yet") {
    database_ptr database;
    REQUIRE_NOTHROW_MESSAGE(database.reset(Database::Open("localhost", "mem:multiple_open.db")), "Opening the first database shouldn't throw");

    database_ptr database2;
    REQUIRE_NOTHROW_MESSAGE(database2.reset(Database::Open("localhost", "mem:multiple_open2.db")), "Opening the a different database database shouldn't throw");

    REQUIRE_THROWS_AS_MESSAGE(Database::Open("localhost", "mem:multiple_open.db"), blobs::exception::DbAlreadyOpen, "Opening the first database a second time should throw an exception");

    // Now close the database
    database.reset();
    REQUIRE_NOTHROW_MESSAGE(database.reset(Database::Open("localhost", "mem:multiple_open.db")), "Opening the first database again after closing it shouldn't throw");
  }
}

