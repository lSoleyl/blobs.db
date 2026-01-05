#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <server/Server.hpp>
#include <server/Logging.hpp>
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

  TODO("Make the log level and log file configurable through cmd line arguments");
  server::logging::Initialize(server::logging::Level::INFO_LEVEL);

  BLOBS_LOG_INFO("Server initializing");

  
  // Use the regular network socket factory for the server.
  network::SocketFactory::Use();

  server::Server server;
  BLOBS_LOG_INFO("Server ready");

  server.ServerMain();

  BLOBS_LOG_INFO("Server exiting");
  server::logging::Shutdown();
  return 0;
}