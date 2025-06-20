
#include <network/Factory.hpp>


#include <exception>

namespace blobs::network {

Factory* Factory::instance = nullptr;


Factory& Factory::Instance() {
  return *instance;
}



void Factory::SetInstance(Factory* factory) {
  if (instance && factory != instance) {
    // Cannot use blobs::exception here as it isn't defined in network
    throw std::exception("Logic Erorr: Multiple different initializations of network::Factory");
  }

  instance = factory;
}


}