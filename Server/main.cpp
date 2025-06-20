#define DOCTEST_CONFIG_IMPLEMENT // we cannot use precompiled headers here becuase of this define
#include "pch.hpp"

#include "Server.hpp"
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

  TODO("Maybe find a better way to initialize the factory? Maybe socket can be the default one?");
  network::SocketFactory::Use();

  server::Server server;
  std::cout << "Server ready\n";

  server.ServerMain();

  std::cout << "Server exiting\n";
  return 0;
}