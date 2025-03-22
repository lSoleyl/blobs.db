#include <network/Resource.hpp>
#include <win_include.hpp>



namespace blobs {
namespace network {


template<> SOCKET Resource<SOCKET>::NullHandle() { return INVALID_SOCKET; }
template<> void Resource<SOCKET>::ReleaseHandle(SOCKET socket) { closesocket(socket); }


template<> addrinfo* Resource<addrinfo*>::NullHandle() { return nullptr; }
template<> void Resource<addrinfo*>::ReleaseHandle(addrinfo* address) { freeaddrinfo(address); }


}
}
