#pragma once

#include "Resource.hpp"
#include "message/Message.hpp"
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
      Client(Server& server, Resource<SOCKET>&& socket);

      Server& server;
      Resource<SOCKET> socket;
      uint32_t id;
      std::string remoteIp; // including the port

      struct Receive {
        WSAOVERLAPPED overlapped;
        WSABUF bufferInfo;
        char buffer[1024];
        DWORD flags;

        size_t writeOffset; // how many bytes of `message` have already been received
        std::unique_ptr<message::Message> message; // the current (incompletely) received message
      };

      Receive receive;


      //TODO: we must somehow process the data stream and convert it into messages


      /** Schedules a WSARecv() call with an optional read buffer offset, which is used in case
       *  the read buffer contains too little data for even the message header to be processed and
       *  we have to wait for more data to arrive.
       */
      void ReceiveData(size_t bufferOffset = 0);

      /** Returns false if the connection has been closed
       */
      void ProcessReceivedData(DWORD bytesTransferred, OVERLAPPED* overlapped);
    };

    /** This will create a Client object from the accepted socket and start listening for data
     *  A reference to the newly created client will be returned
     */
    Client& ProcessAcceptedConnection();

    /** Called by the client object whenever a new message has been fully received.
     */
    void ProcessReceivedMessage(Client& client, std::unique_ptr<message::Message> message);


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