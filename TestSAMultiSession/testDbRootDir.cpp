#include "pch.hpp"
#include <filesystem>

#include <Windows.h>

using namespace blobs;

namespace {

// Copied from common/Encoding
std::string ToUTF8(std::wstring_view content) {
  std::string result;
  auto requiredSize = WideCharToMultiByte(CP_UTF8, NULL, content.data(), content.size(), nullptr, 0, nullptr, nullptr);
  result.resize(requiredSize);
  WideCharToMultiByte(CP_UTF8, NULL, content.data(), content.size(), result.data(), result.size(), nullptr, nullptr);
  return result;
}


}



// Here we will ensure that: 
//  1. No file database outside of the db root dir can be opened
//  2. File databases are recognized as equal independent of casing (at least under windows)
//  3. Intermediate directories are created as needed
//
// The database root directory for the tests is .\test_dbs
TEST_CASE("db root dir semantics") {
  auto session = Session::Create();
  REQUIRE_THROWS_MESSAGE(Database::Open(session, "localhost", "C:\\databases\\test.db"), "Opening a file database outside the db root dir should fail");


  // Open a test database, prepare it and keep it open for now
  database_ptr testDb(Database::Open(session, "localhost", "test.db"));
  testDb->WriteString(0, 0, 0, "test.db");
  Transaction::Commit(session);

  REQUIRE_THROWS_MESSAGE(Database::Open(session, "localhost", "TEST.db"), "Opening the same database with different capitalization in the same session should fail");


  // Opening the same database in another session with different capitalization should not fail and should access the same database
  {
    auto session2 = Session::Create();
    database_ptr testDb2(Database::Open(session2, "localhost", "TEST.db"));
    REQUIRE_MESSAGE(testDb2->ReadString(0, 0, 0, Lock::None) == "test.db", "Opening TEST.db in the second session should access the same file database");
  }

  // Close the database from the first session
  testDb.reset();
  
  // Opening the now closed database again with different capitalization should again open the same database
  testDb.reset(Database::Open(session, "localhost", "TesT.db"));
  REQUIRE_MESSAGE(testDb->ReadString(0, 0, 0, Lock::None) == "test.db", "Re-Opening TesT.db in the fist session should access the same file database");
  

  // Now open another database at a nested path
  database_ptr db2(Database::Open(session, "localhost", "nested\\test.db"));
  


  // Now we expect the following databases to exist:
  //  - .\test_dbs\test.db
  //  - .\test_dbs\nested\test.db 
  REQUIRE_MESSAGE(std::filesystem::exists(L".\\test_dbs\\test.db"), "test.db should exist as a file in the correct location");
  REQUIRE_MESSAGE(std::filesystem::exists(L".\\test_dbs\\nested\\test.db"), "nested\\test.db should exist as a file in the correct location");


  // Now a final test:
  // Opening a file database with an absolute path that is inside the db root dir should be allowed
  auto absolutePath = ToUTF8(std::filesystem::absolute(".\\test_dbs\\test.db").native());
  CAPTURE(absolutePath);

  {
    // Open it in another session, because the main session already has the database opened
    auto session3 = Session::Create();
    database_ptr db;
    REQUIRE_NOTHROW_MESSAGE(db.reset(Database::Open(session3, "localhost", absolutePath)), "Opening test.db with an absolute path should not fail");
    REQUIRE_MESSAGE(testDb->ReadString(0, 0, 0, Lock::None) == "test.db", "Opening test.db through the absolute path should still open the same database.");
  }
}