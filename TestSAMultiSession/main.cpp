#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <blobs/Blobs.hpp>

#include <filesystem>

REGISTER_EXCEPTION_TRANSLATOR(blobs::Exception& ex) {
  return doctest::String(ex.what());
}



int main(int argc, char** argv) {
  // Delete the test_db directory to start with an empty directory for each run
  std::filesystem::remove_all(L".\\test_dbs");

  blobs::InitializeServerLogging(blobs::LogLevel::DEBUG_LEVEL);
  blobs::Initialize(".\\test_dbs");

  // Run unittests
  doctest::Context context;
  //context.setOption("order-by", "name");            // sort the test cases by their name

  context.applyCommandLine(argc, argv);
  context.setOption("no-breaks", false);             // don't break in the debugger when assertions fail

  auto result = context.run();

  blobs::Shutdown();
  return result;
}