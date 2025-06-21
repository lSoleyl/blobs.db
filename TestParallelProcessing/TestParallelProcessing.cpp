#include <blobs/Blobs.hpp>

#include <iostream>
#include <iomanip>


int main() {
  try {
    blobs::Initialize();

    std::cout << "Opening database...\n";
    auto db = blobs::Database::Open("127.0.0.1", "parallel-processing.db");
    std::cout << "Database opened.\n";


    int counter = 0;

    // Every client will write into its own blob so create it before the test
    auto blobId = db->CreateBlob(0, 0, &counter, sizeof(counter));
    blobs::Transaction::Commit();


    // Now read the synchronization point for all clients
    auto syncPoint = db->ReadString(0, 0, 0, true);
    if (syncPoint.empty()) {
      // This is the first client -> print instructions and wait for user to confirm
      std::cout
        << "== Parallel processing test ==\n\n"
        << "This is the first client. Start as many other clients as you wish to run in parallel.\n"
        << "AFTER the clients have opened the database, press any key in this client to actually start the test.\n\n"
        << "The test consists of each client counting up its own counter in its own blob. "
        << "As each client only writes its own blob, there should by no synchronization between the clients. "
        << "Each client will count its blob counter to 100 and then exit.\n\n"
        ;
      system("PAUSE");
      
      // Write something into the blob to prevent other blobs form displaying the message and let all clients run
      db->WriteString<char>(0, 0, 0, "initalized");
      blobs::Transaction::Commit();
    } else {
      // This is a follow blob -> just abort the transaction to release the write lock and continue with the actual processing
      blobs::Transaction::Abort();
    }


    std::cout << "\n\nClient's blob id index is: " << blobId << "\n";

    // Increment the blob counter exatcly 100 times
    for (int i = 0; i < 100; ++i) {
      std::cout << "Waiting for write lock...\n";
      auto currentValue = db->ReadVector<int>(0, 0, blobId, true); // Reading a vector<int>() with 1 element is the same as reading an int
      std::cout << "Reading Value: " << std::setw(3) << currentValue[0] << '\n';
      ++currentValue[0];
      std::cout << "Writing Value: " << std::setw(3) << currentValue[0] << '\n';
      db->WriteVector(0, 0, blobId, currentValue);
      blobs::Transaction::Commit();
    }

    std::cout << "Completed parallel counting. Closing database...\n";
    db->Close();

    blobs::Shutdown();

    std::cout << "Waiting for input to exit\n";
    system("PAUSE");
  } catch (blobs::Exception& ex) {
    std::cerr << "[ERR] Exiting client with exception: " << ex.what() << "\n";
  }
}