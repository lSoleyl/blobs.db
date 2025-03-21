#include <network/Network.hpp>


blobs::network::exception::exception(int wsaErrorCode) : std::exception(GetMessageFromWSAError(wsaErrorCode).c_str()) {}
blobs::network::exception::exception(const char* prefix, int wsaErrorCode) : std::exception((prefix + GetMessageFromWSAError(wsaErrorCode)).c_str()) {}

blobs::network::exception::exception() : exception(WSAGetLastError()) {}
blobs::network::exception::exception(const char* prefix) : exception(prefix, WSAGetLastError()) {}

std::string blobs::network::exception::GetMessageFromWSAError(int wsaErrorCode) {
  char* messageBuffer;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                 wsaErrorCode, NULL, reinterpret_cast<char*>(&messageBuffer), 0, nullptr);

  std::string errorMessage = (messageBuffer != nullptr) ? messageBuffer : "???";
  LocalFree(messageBuffer);
  return errorMessage;
}


namespace blobs {
namespace network {


void Initialize() {
  WSADATA wsaData;
  auto errorCode = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (errorCode) {
    throw exception("WSAStartup() failed with: ", errorCode);
  }
}

void Shutdown() {
  WSACleanup();
}





}
}
