#include "pch.hpp"

using namespace blobs;



TEST_CASE("Open same database in two sessions in one thread") {
  auto sessionA = Session::Create();
  auto sessionB = Session::Create();

  database_ptr dbA;
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, "localhost", "mem:openIn2Sessions.db")), "Opening the database in the first session shouldn't fail");

  database_ptr dbB;
  REQUIRE_NOTHROW_MESSAGE(dbB.reset(Database::Open(sessionB, "localhost", "mem:openIn2Sessions.db")), "Opening the same database in the second session shouldn't fail");
}

// No test for OpenMode::CreateIfNotExist as this is the default mode and is tested well enough throught all remaining tests

/** OpenFailIfNotExist will as the name suggests only open an existing database and not create it.
 */
TEST_CASE("Test OpenMode::OpenFailIfNotExist") {
  auto sessionA = Session::Create();
  auto sessionB = Session::Create();

  // We must use file databases for this test, because memory databases cannot exist if they are not opened
  auto connString = "localhost/OpenMode/OpenFailIfNotExist.db";


  REQUIRE_THROWS_AS_MESSAGE(Database::Open(sessionA, connString, Database::OpenMode::OpenFailIfNotExist), exception::DbDoesNotExist, "Cannot open database if it doesn't exist");

  database_ptr dbA;
  // Now create the database, fill it with data and close it again
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, connString, Database::OpenMode::CreateFailIfExist)), "Creating the database with CreateFailIfExist should not fail");
  dbA->WriteString(0, 0, 0, "content");
  Transaction::Commit(sessionA);
  dbA.reset(); // close database

  // Now opening the database with OpenFailIfNotExist should open the existing database
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, connString, Database::OpenMode::OpenFailIfNotExist)), "OpenFailIfNotExist should not fail if the database exists");
  REQUIRE_MESSAGE(dbA->ReadString(0, 0, 0) == "content", "The opened database should contain the previously written content");

  // Opening the datbase with the same open mode from a second session should also not fail and open the same database
  database_ptr dbB;
  REQUIRE_NOTHROW_MESSAGE(dbB.reset(Database::Open(sessionB, connString, Database::OpenMode::OpenFailIfNotExist)), "OpenFailIfNotExist should not fail when opening the db from second session");
  REQUIRE_MESSAGE(dbB->ReadString(0, 0, 0) == "content", "The opened database in second session should contain the previously written content");
}


/** CreateFailIfExist will as the name suggests only create a non existing database and not overwrite or open an existing one
 */
TEST_CASE("Test OpenMode::CreateFailIfExist") {
  auto sessionA = Session::Create();

  // We must use file databases for this test, because memory databases cannot exist if they are not opened
  auto connString = "localhost/OpenMode/CreateFailIfExist.db";


  database_ptr dbA;
  // Now create the database, fill it with data and close it again
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, connString, Database::OpenMode::CreateFailIfExist)), "Creating the database with CreateFailIfExist should not fail");
  dbA->WriteString(0, 0, 0, "content");
  Transaction::Commit(sessionA);
  dbA.reset(); // close database

  // Now opening the database a second time with CreateFailIfExists must fail
  REQUIRE_THROWS_AS_MESSAGE(dbA.reset(Database::Open(sessionA, connString, Database::OpenMode::CreateFailIfExist)), exception::DbAlreadyExists, "CreateFailIfExist should fail if the database already exists");
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, connString)), "Opening the database in default mode should not fail");
}


/** CreateAlways can only succeed if the database is not opened by any client and will always result in an empty database file.
 */
TEST_CASE("Test OpenMode::CreateAlways") {
  auto sessionA = Session::Create();
  auto sessionB = Session::Create();

  // We must use file databases for this test, because memory databases cannot exist if they are not opened
  auto connString = "localhost/OpenMode/CreateAlways.db";

  database_ptr dbA;
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, connString, Database::OpenMode::CreateAlways)), "Opening the database the first time with CreateAlways should not fail");

  // Write something into the database to be able to later verify that the database is cleared.
  dbA->WriteString(0, 0, 0, "content");
  Transaction::Commit(sessionA);

  REQUIRE_THROWS_AS_MESSAGE(Database::Open(sessionB, connString, Database::OpenMode::CreateAlways), exception::OverwriteOpenedDatabase, "A second client cannot open the database with CreateAlways while it is open");

  // But he can open the database in default open mode
  database_ptr dbB;
  REQUIRE_NOTHROW_MESSAGE(dbB.reset(Database::Open(sessionB, connString)), "The second client should be able to open the database in default open mode.");
  REQUIRE_MESSAGE(dbB->ReadString(0,0,0) == "content", "Expected other value in database after opening it in the second session");
  dbB.reset(); // <- close the database to be able to re-open the database in the first session with CreateAlways.

  dbA.reset(); // <- close the first database to be able to reopen it with CreateAlways without conflict.
  REQUIRE_NOTHROW_MESSAGE(dbA.reset(Database::Open(sessionA, connString, Database::OpenMode::CreateAlways)), "Re-Opening the database with CreateAlways should not fail if the database is not opened.");

  REQUIRE_MESSAGE(dbA->ReadString(0, 0, 0) == "", "After re-opening the database with CreateAlways the database should be empty again");
}


/** Test to ensure that opening more than one database per session behaves as expected
 */
TEST_CASE("Test open 2 databases in single session") {
  auto session = Session::Create();

  // Here open both databses before starting the transaction
  database_ptr dbA(Database::Open(session, "localhost/mem:testOpenMultipleA"));
  database_ptr dbB(Database::Open(session, "localhost/mem:testOpenMultipleB"));

  REQUIRE(dbA->ReadString(0, 0, 0) == "");
  REQUIRE(dbB->ReadString(0, 0, 0) == "");

  dbA->WriteString(0, 0, 0, "a");
  dbB->WriteString(0, 0, 0, "b");

  Transaction::Commit(session);

  {
    // Now check in a second session that the data has actually been written 
    auto session = Session::Create();

    database_ptr dbA(Database::Open(session, "localhost/mem:testOpenMultipleA"));
    REQUIRE(dbA->ReadString(0, 0, 0) == "a");

    // Here open the second db after starting the transaction
    database_ptr dbB(Database::Open(session, "localhost/mem:testOpenMultipleB"));
    REQUIRE(dbB->ReadString(0, 0, 0) == "b");
  }
}
