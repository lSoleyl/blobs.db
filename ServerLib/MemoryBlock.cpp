#include "pch.hpp"
#include "include\server\MemoryBlock.hpp"


namespace blobs::server {

MemoryBlock::MemoryBlock() : offset(0), size(0), status(Status::LOADED) {}

}
