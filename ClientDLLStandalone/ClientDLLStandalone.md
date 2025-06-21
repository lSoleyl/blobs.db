# ClientDLLStandlone

This projects builds the DLL (blobs.sa.dll) client programm to implement a standalone client where server and client run in the same process.
This allows the development of SQLite like applications with blobs.db.

The interface is the same as the regular blobs.db.dll, so the dll can simply be swapped out without any code change.

## Usage

```
#include <blobs/Blobs.hpp>

int main() {
  try {
    // Initialize blobs.db and start the local server
    blobs::Initialize();

    // Connect to the (local) database server and open the database
    // The hostname parameter is completely ignored in the standalone version
    auto db = blobs::Database::Open("127.0.0.1", "test.db");

    // Write a string into the first blob (which always exists) and commit the transaction
    db->WriteString(0, 0, 0, "Hello blobs.db");
    blobs::Transaction::Commit();

    // Read the string back and print it to std::out
    std::cout << db->ReadString<char>(0, 0, 0);

    // Close the database again
    db->Close();

    // Shutdown the local server and cleanup resources
    blobs::Shutdown();


  } catch (blobs::Exception& ex) {
    std::cerr << "[ERR] Exiting client with exception: " << ex.what() << "\n";
  }
}

```