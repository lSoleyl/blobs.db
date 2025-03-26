#pragma once


// For some reason winsock complains about WSAAddressToStringA() being deprecated... But why should I use the unicode version if I don't need unicode here?
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <MSWSock.h>
#include <processthreadsapi.h>

#pragma comment(lib, "Ws2_32.lib")
