#include <blobs/Blobs.hpp>

#include <iostream>

int main()
{
  try {
    blobs::Initialize();

    std::cout << "Starting client\n";
    auto db = blobs::Database::Open("127.0.0.1", "test.db");

    // First read content at blob 0
    auto content = db->ReadString(0, 0, 0);
    std::cout << "Content at (0,0,0): '" << content << "'\n";

    // Write some data into the initial blob and commit the txn
    if (content.empty()) {
      db->WriteString(0, 0, 0, "Hello blobs.db!");
    } else {
      db->WriteString(0, 0, 0, content + " (modified)");
    }
    blobs::Transaction::Commit();

     
    TODO(
      "Once the server is operational, I should implement some nice example programs to verify the correct working of the database. "
      "99 bottles of beer for example where each transaction has to read through all previous messages (1 blob per message) and then add the next blob. "
      "This could then be performed by multiple clients in parallel."
    );

    db->Close();
    blobs::Shutdown();

    std::cout << "Exiting client\n";
    system("PAUSE");
  } catch (blobs::Exception& ex) {
    std::cerr << "[ERR] Exiting client with exception: " << ex.what() << "\n";
  }
}
