#include "pch.hpp"

using namespace blobs;


// A basic read-write deadlock scenario where both clients first 
// hold a read lock on the same blob and then attempt to upgrade it into a write lock, which must fail
TEST_CASE("Read-Write deadlock scenario") {
  // First prepare the database in the main session
  database_ptr mainDb(Database::Open("localhost", "mem:testRWDeadlock"));
  mainDb->WriteString(0, 0, 0, "<empty>");
  Transaction::Commit();


  std::vector<std::string> threadNames = { "A", "B" };

  const std::string* successfulThread = nullptr;

  // Now create a deadlock where both clients first acquire a read lock on the same blob and then attempt to upgrade
  // it into a write lock later in the same transaction
  parallel::sync_point syncPoint(2);
  
  REQUIRE_THROWS_AS_MESSAGE(parallel::for_each(threadNames, [&](const std::string& threadName) {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, "localhost", "mem:testRWDeadlock"));
    // First read the blob
    CAPTURE(threadName);
    std::string content;

    // The first read cannot block or cause a deadlock (multiple readers per blob are allowed)
    CHECK_NOTHROW(content = db->ReadString(0, 0, 0));
    CHECK(content == "<empty>");
    syncPoint.wait();


    // Now both threads will attempt to write into the same blob in the same transaction
    // This will result in a deadlock exception for one of the two clients
    db->WriteString(0, 0, 0, threadName);
    Transaction::Commit(session);
    successfulThread = &threadName;

  }), blobs::exception::Deadlock, "Expected deadlock exception not thrown");


  // Finally confirm that the blob has been correctly updated in the main session
  REQUIRE(successfulThread != nullptr);
  REQUIRE(mainDb->ReadString(0, 0, 0) == *successfulThread);
}



// A write-write deadlock scenario where they acquire write locks to two different blobs in a different order.
// This causes a deadlock and one of the operations must fail
TEST_CASE("Write-Write deadlock scenario") {
  // First prepare the database in the main session
  database_ptr mainDb(Database::Open("localhost", "mem:testWWDeadlock"));
  auto blob0 = mainDb->CreateString(0, 0, "<empty>");
  auto blob1 = mainDb->CreateString(0, 0, "<empty>");
  Transaction::Commit();


  blob_id blobs[2] = { blob0, blob1 };

  std::vector<int> threadIds = { 0, 1 };
  std::atomic<int> successfulThread(-1);

  parallel::sync_point syncPoint(2);
  REQUIRE_THROWS_AS_MESSAGE(parallel::for_each(threadIds, [&](int threadId) {
    auto session = Session::Create();
    database_ptr db(Database::Open(session, "localhost", "mem:testWWDeadlock"));
    CAPTURE(threadId);
    auto blobId = blobs[threadId];
    CAPTURE(blobId);
    // Acquire the write lock and ensure the blob hasn't been written to yet
    CHECK(db->ReadString(0, 0, blobId, true) == "<empty>");

    // Now wait for the other thread
    syncPoint.wait();

    blobId = blobs[(threadId + 1) % 2];
    auto content = db->ReadString(0, 0, blobId);
    CHECK_MESSAGE(content == "<empty>", "The second blob should still be empty");
    
    // This is the successful thread -> write into both blobs the threadid
    // The following calls cannot fail anymore
    REQUIRE_NOTHROW(db->WriteString(0, 0, blobs[0], std::to_string(threadId)));
    REQUIRE_NOTHROW(db->WriteString(0, 0, blobs[1], std::to_string(threadId)));
    successfulThread = threadId;

    Transaction::Commit(session);
  }), blobs::exception::Deadlock, "Expected deadlock exception not thrown!");


  REQUIRE_MESSAGE(successfulThread.load() != -1, "At least one thread should have succeeded");
  REQUIRE_MESSAGE(mainDb->ReadString(0, 0, blobs[0]) == std::to_string(successfulThread), "First blob should hold the success thread id");
  REQUIRE_MESSAGE(mainDb->ReadString(0, 0, blobs[1]) == std::to_string(successfulThread), "Second blob should hold the success thread id");
}
