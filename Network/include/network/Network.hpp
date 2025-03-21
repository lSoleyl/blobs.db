#pragma once

#include "..\win_include.hpp"
#include "Server.hpp"
#include "Client.hpp"


namespace blobs {
namespace network {

/** Call to initialize Winsock
 */
void Initialize();

/** Wrapper around WSACleanup()
 */
void Shutdown();

/** A generic networking exception
 */
class exception : public std::exception {
public: 
  exception(); // Calls WSAGetLastError() to retrieve the error code
  exception(const char* prefix); // Calls WSAGetLastError() to retrieve the error code
  exception(int wsaErrorCode);
  exception(const char* prefix, int wsaErrorCode);

private:
  static std::string GetMessageFromWSAError(int wsaErrorCode);
};

}
}