#include "pch.hpp"


using namespace blobs;


namespace {
  template<typename T>
  std::vector<T> intoVector(blobs::Range<T>&& range) {
    return std::vector<T>(range.begin(), range.end());
  }
}


TEST_CASE("Dirty read blobs during write lock") {
  auto dbName = "mem:testSimpleDirtyRead";
  database_ptr db(Database::Open("localhost", dbName));
  db->WriteString(0, 0, 0, "before");
  Transaction::Commit();


  std::atomic<std::chrono::high_resolution_clock::time_point> readDuringUpdate, updateCommitted;


  
  parallel::sync_point beforeUpdate(2);
  parallel::sync_point duringUpdate(2);
  parallel::sync_point afterCommit(2);

  parallel::run({
    // The first client will simply wait and then update the blob
    [&](){
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      beforeUpdate.wait();
      db->WriteString(0, 0, 0, "after");
      duringUpdate.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // long enough to determine whether the second client is blocked or not 
      updateCommitted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
      afterCommit.wait();
    },


    // The second client will first perform a dirty read, then sync with the other client, then 
    // perform another dirty read before the other client completed its transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK_MESSAGE(db->ReadString(0, 0, 0, Lock::None) == "before", "Wrong blob content before the update");
      beforeUpdate.wait();
      duringUpdate.wait();
      // Now read the blob during the other client's update transaction (he is holding the write lock)
      CHECK_MESSAGE(db->ReadString(0, 0, 0, Lock::None) == "before", "Wrong blob content during the update transaction");
      readDuringUpdate = std::chrono::high_resolution_clock::now();
      afterCommit.wait();

      // And finally read the blob again after the commit
      CHECK_MESSAGE(db->ReadString(0, 0, 0, Lock::None) == "after", "Wrong blob content after the update transaction committed");

      CHECK_MESSAGE(Transaction::IsRunning(session) == false, "The dirty read operations shouldn't have started a transaction");
    }
  });


  REQUIRE_MESSAGE(readDuringUpdate.load() < updateCommitted.load(), "The dirty read should have completed BEFORE the write transaction");
}


TEST_CASE("Dirty read of GetAllBlobs()") {
  auto dbName = "mem:testDirtyReadGetAllBlobs";

  std::atomic<std::chrono::high_resolution_clock::time_point> readDuringTxn, updateCommitted;

  parallel::sync_point beforeDeleteTxn(2);
  parallel::sync_point duringDeleteTxn(2);
  parallel::sync_point afterCommit(2);

  parallel::run({
    // The first client will simply delete the default blob and create a new one
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      beforeDeleteTxn.wait();
      db->DeleteBlob(0, 0, 0);
      db->CreateString(0, 0, "created");
      db->CreateString(0, 0, "created2");
      duringDeleteTxn.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // long enough to determine whether the second client is blocked or not 
      updateCommitted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
      afterCommit.wait();
    },


    // The second client will first perform a dirty read, then sync with the other client, then 
    // perform another dirty read before the other client completed its transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK_MESSAGE(intoVector(db->GetAllBlobs(0, 0, Lock::None)) == (std::vector<blob_id> { 0 }), "Wrong blob list before the transaction");
      beforeDeleteTxn.wait();
      duringDeleteTxn.wait();
      // Now read the blob list during the other client's update transaction (he is holding the write lock)
      CHECK_MESSAGE(intoVector(db->GetAllBlobs(0, 0, Lock::None)) == (std::vector<blob_id> { 0 }), "Wrong blob list during the transaction");
      readDuringTxn = std::chrono::high_resolution_clock::now();
      afterCommit.wait();

      // And finally read the blob list again after the commit
      CHECK_MESSAGE(intoVector(db->GetAllBlobs(0, 0, Lock::None)) == (std::vector<blob_id> { 1, 2 }), "Wrong blob list after the commit");

      CHECK_MESSAGE(Transaction::IsRunning(session) == false, "The dirty read operations shouldn't have started a transaction");
    }
  });

  REQUIRE_MESSAGE(readDuringTxn.load() < updateCommitted.load(), "The dirty read should have completed BEFORE the write transaction");
}


TEST_CASE("Dirty read of GetAllClusters()") {
  auto dbName = "mem:testDirtyReadGetAllClusters";

  std::atomic<std::chrono::high_resolution_clock::time_point> readDuringTxn, updateCommitted;

  parallel::sync_point beforeDeleteTxn(2);
  parallel::sync_point duringDeleteTxn(2);
  parallel::sync_point afterCommit(2);

  parallel::run({
    // The first client will simply delete the default cluster and create a new one
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      beforeDeleteTxn.wait();
      db->DeleteCluster(0, 0);
      db->CreateCluster(0);
      db->CreateCluster(0);
      duringDeleteTxn.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // long enough to determine whether the second client is blocked or not 
      updateCommitted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
      afterCommit.wait();
    },


    // The second client will first perform a dirty read, then sync with the other client, then 
    // perform another dirty read before the other client completed its transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK_MESSAGE(intoVector(db->GetAllClusters(0, Lock::None)) == (std::vector<cluster_id> { 0 }), "Wrong cluster list before the transaction");
      beforeDeleteTxn.wait();
      duringDeleteTxn.wait();
      // Now read the cluster list during the other client's update transaction (he is holding the write lock)
      CHECK_MESSAGE(intoVector(db->GetAllClusters(0, Lock::None)) == (std::vector<cluster_id> { 0 }), "Wrong cluster list during the transaction");
      readDuringTxn = std::chrono::high_resolution_clock::now();
      afterCommit.wait();

      // And finally read the cluster list again after the commit
      CHECK_MESSAGE(intoVector(db->GetAllClusters(0, Lock::None)) == (std::vector<cluster_id> { 1, 2 }), "Wrong cluster list after the commit");

      CHECK_MESSAGE(Transaction::IsRunning(session) == false, "The dirty read operations shouldn't have started a transaction");
    }
    });

  REQUIRE_MESSAGE(readDuringTxn.load() < updateCommitted.load(), "The dirty read should have completed BEFORE the write transaction");
}


TEST_CASE("Dirty read of GetAllSegments()") {
  auto dbName = "mem:testDirtyReadGetAllSegments";

  std::atomic<std::chrono::high_resolution_clock::time_point> readDuringTxn, updateCommitted;

  parallel::sync_point beforeDeleteTxn(2);
  parallel::sync_point duringDeleteTxn(2);
  parallel::sync_point afterCommit(2);

  parallel::run({
    // The first client will simply delete the default cluster and create a new one
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      beforeDeleteTxn.wait();
      db->DeleteSegment(0);
      db->CreateSegment();
      db->CreateSegment();
      duringDeleteTxn.wait();
      std::this_thread::sleep_for(std::chrono::milliseconds(50)); // long enough to determine whether the second client is blocked or not 
      updateCommitted = std::chrono::high_resolution_clock::now();
      Transaction::Commit(session);
      afterCommit.wait();
    },


    // The second client will first perform a dirty read, then sync with the other client, then 
    // perform another dirty read before the other client completed its transaction
    [&]() {
      auto session = Session::Create();
      database_ptr db(Database::Open(session, "localhost", dbName));
      CHECK_MESSAGE(intoVector(db->GetAllSegments(Lock::None)) == (std::vector<segment_id> { 0 }), "Wrong segment list before the transaction");
      beforeDeleteTxn.wait();
      duringDeleteTxn.wait();
      // Now read the segment list during the other client's update transaction (he is holding the write lock)
      CHECK_MESSAGE(intoVector(db->GetAllSegments(Lock::None)) == (std::vector<segment_id> { 0 }), "Wrong segment list during the transaction");
      readDuringTxn = std::chrono::high_resolution_clock::now();
      afterCommit.wait();

      // And finally read the segment list again after the commit
      CHECK_MESSAGE(intoVector(db->GetAllSegments(Lock::None)) == (std::vector<segment_id> { 1, 2 }), "Wrong segment list after the commit");

      CHECK_MESSAGE(Transaction::IsRunning(session) == false, "The dirty read operations shouldn't have started a transaction");
    }
    });

  REQUIRE_MESSAGE(readDuringTxn.load() < updateCommitted.load(), "The dirty read should have completed BEFORE the write transaction");
}
