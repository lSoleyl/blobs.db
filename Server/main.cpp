#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <server/Server.hpp>
#include <network/SocketFactory.hpp>

#include <iostream>
#include <io.h>
#include <fcntl.h>

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

  // Enable unicode console output
  SetConsoleOutputCP(CP_UTF8);

  // Enable buffering on stdio for faster writes... It also results in us having to flush the output using std::endl
  setvbuf(stdout, nullptr, _IOFBF, 1000);

  std::cout << "Server initializing" << std::endl;;

  
  // Use the regular network socket factory for the server.
  network::SocketFactory::Use();

  server::Server server;
  std::cout << "Server ready" << std::endl;

  server.ServerMain();

  std::cout << "Server exiting" << std::endl;
  return 0;
}