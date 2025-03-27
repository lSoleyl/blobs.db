#pragma once

#include "Resource.hpp"
#include "..\win_include.hpp"

#include <thread>
#include <atomic>
#include <list>


namespace blobs {
namespace network {
  
class Server {
  public: 
    /** The constructor will immediately start listening
     */
    Server(int listenPort);
    ~Server();
    // Server instance is not copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

  private:
    void ListenThreadMain();
    Resource<addrinfo*> GetListenAddress() const;

    /** This method simply creates an IPv6 socket in dual stack mode, performs error handling and returns the socket.
     */
    Resource<SOCKET> CreateDualStackSocket() const;

    /** Returns true if the connection has completed immediately
     */
    bool AcceptNewConnection();

    struct Client {
      Client(Resource<SOCKET>&& socket);

      Resource<SOCKET> socket;
      WSAOVERLAPPED receiveOverlapped;
      WSABUF receiveBufferInfo;
      char receiveBuffer[1024];
      DWORD recvFlags;
      uint32_t id;

      std::string remoteIp; // including the port

      void ReceiveData();

      /** Returns false if the connection has been closed
       */
      void ProcessReceivedData(DWORD bytesTransferred, OVERLAPPED* overlapped);
    };

    /** This will create a Client object from the accepted socket and start listening for data
     *  A reference to the newly created client will be returned
     */
    Client& ProcessAcceptedConnection();


    struct AcceptData {
      Resource<SOCKET> socket;
      WSAOVERLAPPED overlapped;
      char buffer[128];
    };


    std::thread networkThread;
    const int listenPort;
    Resource<SOCKET> listenSocket;
    Resource<HANDLE> ioCompletionPort;
    std::list<Client> clients; // all connected clients
    std::atomic<bool> running;

    AcceptData accept; // structure containing the necessary fields for the current async accept call
};


}
}