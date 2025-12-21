#include "pch.hpp"

using namespace blobs;



TEST_CASE("Create blobs from multiple threads") {
  SUBCASE("Create 10 consecutive blobs per txn in 2 sessions with commit") {
    std::vector<std::vector<std::pair<blob_id, std::chrono::high_resolution_clock::time_point>>> createdBlobs(2); // <- 2 threads
    
    parallel::sync_point syncPoint(2);
  
    parallel::for_each(createdBlobs, [&](auto& createdBlobs) {
      syncPoint.wait(); // let the 2 threads run as close to parallel as possible
  
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", "mem:create10BlobsCommit.db"));
  
      auto firstId = db->CreateBlob(0, 0, nullptr, 0);
      createdBlobs.push_back(std::make_pair(firstId, std::chrono::high_resolution_clock::now()));
      for (int i = 1; i < 10; ++i) {
        auto id = db->CreateBlob(0, 0, nullptr, 0);
        createdBlobs.push_back(std::make_pair(id, std::chrono::high_resolution_clock::now()));
        REQUIRE(id == firstId + i); // blob ids must be sequentially ascending
      }
  
      Transaction::Commit(session);
    });
  
    // Sort by blob id
    std::sort(createdBlobs.begin(), createdBlobs.end(), [](auto& a, auto& b) { return a[0].first < b[0].first; });
  
    REQUIRE(createdBlobs[0][0].first == 1); // First created blob should be 1
    REQUIRE(createdBlobs[1][9].first == 20); // Last created blob should be 20
  
    // The last blob of the first finished client must be created BEFORE the first blob of the second client
    REQUIRE(createdBlobs[0][9].second < createdBlobs[1][0].second);
  }


  SUBCASE("Create 10 consecutive blobs per txn in 3 sessions with abort") {
    std::vector<std::vector<std::pair<blob_id, std::chrono::high_resolution_clock::time_point>>> createdBlobs(3); // <- 3 threads

    parallel::sync_point syncPoint(3);

    parallel::for_each(createdBlobs, [&](auto& createdBlobs) {
      syncPoint.wait(); // let the 2 threads run as close to parallel as possible

      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", "mem:create10BlobsAbort.db"));

      auto firstId = db->CreateBlob(0, 0, nullptr, 0);
      REQUIRE(firstId == 1); // Now since we always abort the transaction, each session will start with blob id of 1
      createdBlobs.push_back(std::make_pair(firstId, std::chrono::high_resolution_clock::now()));
      for (int i = 1; i < 10; ++i) {
        auto id = db->CreateBlob(0, 0, nullptr, 0);
        createdBlobs.push_back(std::make_pair(id, std::chrono::high_resolution_clock::now()));
        REQUIRE(id == firstId + i); // blob ids must be sequentially ascending
      }
      
      Transaction::Abort(session); // don't commit created blobs
    });

    // Sort by creation timestamp
    std::sort(createdBlobs.begin(), createdBlobs.end(), [](auto& a, auto& b) { return a[0].second < b[0].second; });

    // Now since we aborted, all of the first ids should be 1 and the last ones 10
    REQUIRE(createdBlobs[0][0].first == 1);
    REQUIRE(createdBlobs[0][9].first == 10);
    REQUIRE(createdBlobs[1][0].first == 1);
    REQUIRE(createdBlobs[1][9].first == 10);
    REQUIRE(createdBlobs[2][0].first == 1);
    REQUIRE(createdBlobs[2][9].first == 10);

    // But beacause the sessions were working on the same data, they still must wait for the previous one to finish, 
    // so their timestamps should be strictly ordered.
    REQUIRE(createdBlobs[0][9].second < createdBlobs[1][0].second);
    REQUIRE(createdBlobs[1][9].second < createdBlobs[2][0].second);
  }

}