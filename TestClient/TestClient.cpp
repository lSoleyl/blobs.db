#include <blobs/Database.hpp>

#include <iostream>

int main()
{
  std::cout << "Starting client\n";
  blobs::Database::Open("127.0.0.1:8888");
  std::cout << "Exiting client\n";
}
