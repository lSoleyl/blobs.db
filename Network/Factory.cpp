
#include <network/Factory.hpp>

#include <common/Exception.hpp>

#include <exception>

namespace blobs::network {

Factory* Factory::instance = nullptr;


Factory& Factory::Instance() {
  if (!instance) {
    // Give the client a hint as to how to fix this problem.
    throw blobs::Exception("Missing call to blobs::Initialize()!");
  }
  return *instance;
}



void Factory::SetInstance(Factory* factory) {
  if (instance && factory != instance) {
    throw blobs::Exception("Logic Error: Multiple different initializations of network::Factory");
  }

  instance = factory;
}


}