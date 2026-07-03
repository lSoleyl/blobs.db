#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <server/Server.hpp>
#include <server/Logging.hpp>
#include <network/SocketFactory.hpp>

#include <common/Encoding.hpp>

#include "Args.hpp"

#include <iostream>
#include <io.h>
#include <fcntl.h>

using namespace blobs;


/** Server main can be run with --test to run all unittest
 */
int wmain(int argc, wchar_t** argv) {
  auto args = Args::Parse(argc, argv);
  if (!args) {
    return 1; // illegal arguments passed
  }

  std::cout << "blobs.db server v" << version::major << '.' << version::minor << '.' << version::patch << '\n';

  if (args.help) {
    // Print help and exit
    args.PrintHelp();
    return 0;
  }

#ifndef DOCTEST_CONFIG_DISABLE
  if (args.runTests) {
    // Run unittests
    doctest::Context context;
    //context.setOption("order-by", "name");            // sort the test cases by their name

    context.applyCommandLine(argc, Args::ToAsciiArgv(argc, argv));
    context.setOption("no-breaks", true);             // don't break in the debugger when assertions fail

    return context.run();
  }
#endif


  server::logging::Initialize(args.config);
  BLOBS_LOG_INFO("Server initializing");

  
  // Use the regular network socket factory for the server.
  network::SocketFactory::Use();

  server::Server server(args.config);
  BLOBS_LOG_INFO("Server ready");

  server.ServerMain();

  BLOBS_LOG_INFO("Server exiting");
  server::logging::Shutdown();
  return 0;
}