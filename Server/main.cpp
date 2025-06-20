#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <server/Server.hpp>
#include <network/SocketFactory.hpp>

#include <iostream>

using namespace blobs;

/** Server main can be run with --test to run all unittest
 */
int main(int argc, char** argv) {
#ifndef DOCTEST_CONFIG_DISABLE
  if (argc >= 2 && std::string_view(argv[1]) == "--test") {
    // Run unittests
    doctest::Context context;
    //context.setOption("order-by", "name");            // sort the test cases by their name

    context.applyCommandLine(argc, argv);
    context.setOption("no-breaks", true);             // don't break in the debugger when assertions fail

    return context.run();
  }
#endif


  std::cout << "Server initializing\n";

  TODO("process outstanding transactions from the server log if any"); 
  // But to do this, the server log would need to be stored separately from the databse
  // Alternatively process the transaction log when opening a database for the first time.

  // Use the regular network socket factory for the server.
  network::SocketFactory::Use();

  server::Server server;
  std::cout << "Server ready\n";

  server.ServerMain();

  std::cout << "Server exiting\n";
  return 0;
}