#pragma once

#include "Resource.hpp"
#include "message/Message.hpp"
#include "IOCompletionHandler.hpp"
#include "DuplexMessageSocket.hpp"
#include "..\win_include.hpp"

#include <thread>
#include <atomic>
#include <list>


namespace blobs {
namespace network {
  
class Server final : public IOCompletionHandler {
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

    /** Calls AcceptEx() to accept the next connection
     */
    void AcceptNewConnection();

    /** IOCompletion handler for listen socket (effectively just accepts the new connection)
     */
    virtual void HandleIOCompletion(DWORD bytesTransferred, OVERLAPPED* overlapped) override;


    struct Client : public DuplexMessageSocket {
      Client(Server& server, Resource<SOCKET>&& socket, HANDLE ioCompletionPort);

      /** Connection to client closed
       */
      virtual void HandleSocketClosed() override;

      /** Handle new incoming message
       */
      virtual void HandleMessageReceived(std::unique_ptr<message::Message> message) override;

      Server& server;
      uint32_t id;
      std::string remoteIp; // including the port
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