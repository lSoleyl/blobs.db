#include "pch.hpp"

using namespace blobs;


// Here we simply check that the session locking mechanism works properly and all threads access to the database
// will be serialized. Since the threads in this test will all access their own blob there is no logical issue with 
// operations interleaving. The session's state should not be messed up by this.
TEST_CASE("Multiple threads accessing the database through a single session") {
  // Create own session for this to avoid impacting other tests in case something goes wrong here
  auto session = Session::Create();
  database_ptr db(Database::Open(session, "localhost", "mem:testMultipleThreadsSingleSession"));

  const int threadCount = 5;
  std::vector<int> threads(threadCount);
  std::iota(threads.begin(), threads.end(), 0);
  parallel::sync_point syncPoint(threadCount);

  parallel::for_each(threads, [&](int threadId) {
    CAPTURE(threadId);
    syncPoint.wait(); // let all threads run from here simultaneously to also ensure correct locking during blob creation
    int number = 0;
    auto blobId = db->CreateBlob(0, 0, &number, sizeof(number));
    CAPTURE(blobId);
    Transaction::Commit(session);

    // Now perform 10 increments
    for (int i = 0; i < 10; ++i) { 
      int number = *static_cast<const int*>(db->ReadBlob(0, 0, blobId).first);
      REQUIRE(number == i);
      ++number;
      db->WriteBlob(0, 0, blobId, &number, sizeof(number));
      Transaction::Commit(session);
    }
  });


  // Now read the blobs in the main session to ensure that all data has been written correctly in the 
  // global session. Performing this check actually revealed a bug in the sticky previous lock implementation.
  {
    database_ptr db(Database::Open("localhost", "mem:testMultipleThreadsSingleSession"));
    for (int blobId = 1; blobId < threadCount; ++blobId) {
      CAPTURE(blobId);
      int number = *static_cast<const int*>(db->ReadBlob(0, 0, blobId).first);
      REQUIRE(number == 10);
    }
  }
}
