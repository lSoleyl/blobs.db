#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <blobs/Blobs.hpp>


REGISTER_EXCEPTION_TRANSLATOR(blobs::Exception& ex) {
  return doctest::String(ex.what());
}



int main(int argc, char** argv) {
  blobs::Initialize();

  // Run unittests
  doctest::Context context;
  //context.setOption("order-by", "name");            // sort the test cases by their name

  context.applyCommandLine(argc, argv);
  context.setOption("no-breaks", true);             // don't break in the debugger when assertions fail

  auto result = context.run();

  blobs::Shutdown();
  return result;
}