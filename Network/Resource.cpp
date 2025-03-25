#include <network/Resource.hpp>
#include <win_include.hpp>



namespace blobs {
namespace network {

template<> SOCKET ResourceDefinition<SOCKET>::NullHandle() { return INVALID_SOCKET; }
template<> void ResourceDefinition<SOCKET>::ReleaseHandle(SOCKET socket) { closesocket(socket); }

template<> addrinfo* ResourceDefinition<addrinfo*>::NullHandle() { return nullptr; }
template<> void ResourceDefinition<addrinfo*>::ReleaseHandle(addrinfo* address) { freeaddrinfo(address); }

template<> HANDLE ResourceDefinition<HANDLE>::NullHandle() { return INVALID_HANDLE_VALUE; }
template<> void ResourceDefinition<HANDLE>::ReleaseHandle(HANDLE handle) { CloseHandle(handle); }

}}
