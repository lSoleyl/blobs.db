#include <blobs/Blobs.hpp>

#include <iostream>

int main()
{
  try {
    std::cout << "Starting client\n";
    auto db = blobs::Database::Open("127.0.0.1:8888");

    //TODO: once the server is operational, I should implement some nice example programs to verify the correct working of the database
    //TODO: 99 bottles of beer for example where each transaction has to read through all previous messages (1 blob per message) and then add the next blob.
    //      This could then be performed by multiple clients in parallel.

    db->Close();
    std::cout << "Exiting client\n";
  } catch (blobs::Exception& ex) {
    std::cerr << "[ERR] Exiting client with exception: " << ex.what() << "\n";
  }
}
