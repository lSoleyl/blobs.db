#include "pch.hpp"

using namespace blobs;


// Here we will commit 100.000 blobs, which will require two commit messages. 
// This will ensure that big commits are handled correctly
TEST_CASE("test huge commit") {
  auto session = Session::Create();
  auto dbName = "mem:testHugeCommit";
  database_ptr db(Database::Open(session, "localhost", dbName));

  // Create 100.000 blobs with each one holding its index as value
  constexpr blob_id N_BLOBS = 100000;
  for (blob_id i = 1; i <= N_BLOBS; ++i) {
    db->CreateBlob(0, 0, &i, sizeof(i));
  }

  REQUIRE_NOTHROW_MESSAGE(Transaction::Commit(session), "Committing 100.000 blobs should not fail");

  // Now validate in a separate session that the most important blobs exist
  {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, "localhost", dbName));

    std::vector<blob_id> checkIds = { 1, 10, 100, 123, 1000, 7777, 10000, 100000 };
    for (auto checkId : checkIds) {
      CAPTURE(checkId);
      REQUIRE(checkId == *static_cast<const blob_id*>(db->ReadBlob(0, 0, checkId).first));
    }
  }
}