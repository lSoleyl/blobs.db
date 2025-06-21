# ClientDLL

This project builds the DLL (blobs.db.dll) to link into the client programm to be able to connect to blobs.db and actually talk to the database server.
It will provide some needed abstractions to avoid writing much boilerplate.

## Usage

```
#include <blobs/Blobs.hpp>

int main() {
  try {
    // Initialize
    blobs::Initialize();

    // Connect to the database server and open the database
    auto db = blobs::Database::Open("127.0.0.1", "test.db");

    // Write a string into the first blob (which always exists) and commit the transaction
    db->WriteString(0, 0, 0, "Hello blobs.db");
    blobs::Transaction::Commit();

    // Read the string back and print it to std::out
    std::cout << db->ReadString<char>(0, 0, 0);

    // Close the database again
    db->Close();

    // Cleanup resources
    blobs::Shutdown();


  } catch (blobs::Exception& ex) {
    std::cerr << "[ERR] Exiting client with exception: " << ex.what() << "\n";
  }
}

```