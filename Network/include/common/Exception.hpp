#pragma once


// This header is just a forward to <blobs/Exception.hpp> to be able to use blobs::Exception in this project
// even though we cannot define the header here, because the Header must be visible to the Client using the ClientDLL
// and thus must be placed in the ClientLib headers folder... at least for now

#include "../../ClientLib/include/blobs/Exception.hpp"
