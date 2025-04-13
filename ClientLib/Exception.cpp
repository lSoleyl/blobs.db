#include "pch.hpp"
#include <blobs/Exception.hpp>

namespace blobs {

Exception::Exception(std::string_view reason) : reason(reason) {}
Exception::~Exception() {}


const char* Exception::what() const {
  return reason.c_str();
}



}