
#include <network/Factory.hpp>


#include <exception>

namespace blobs::network {

Factory* Factory::instance = nullptr;


Factory& Factory::Instance() {
  if (!instance) {
    // Give the client a hint as to how to fix this problem.
    std::cerr << "Missing call to blobs::Initialize()!";
    throw std::exception("Missing call to blobs::Initialize()!");
  }
  return *instance;
}



void Factory::SetInstance(Factory* factory) {
  if (instance && factory != instance) {
    // Cannot use blobs::exception here as it isn't defined in network
    std::cerr << "Logic Error: Multiple different initializations of network::Factory";
    throw std::exception("Logic Error: Multiple different initializations of network::Factory");
  }

  instance = factory;
}


}