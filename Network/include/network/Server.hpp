#pragma once

#include "MessagePointer.hpp"
#include "DuplexMessageSocket.hpp"
#include "ReceiveMessageQueue.hpp"
#include "..\win_include.hpp"
#include "message/DatabaseOpenResponse.hpp"

#include <thread>
#include <atomic>
#include <list>
#include <unordered_map>


namespace blobs {
namespace network {
  
class Server final : private IOCompletionHandler {
  public: 
    /** The constructor will immediately start listening
     */
    Server(int listenPort);
    ~Server();
    // Server instance is not copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;


    /** Wait for the next sever message without a timeout
     */
    MessagePointer AwaitMessage();


    void SendDatabaseOpenResponse(uint16_t client, message::DatabaseOpenResponse::Result result, uint8_t dbId = 0);

    /** Send an already allocated message to the specified client
     */
    void SendMessageToClient(uint16_t client, MessagePointer message);
    
    

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
      Client(Server& server, uint16_t id, Resource<SOCKET>&& socket);

      /** Connection to client closed
       */
      virtual void HandleSocketClosed() override;

      /** Handle new incoming message
       */
      virtual void HandleMessageReceived(MessagePointer message) override;

      Server& server;

      /** client id is only 16 bit (see message::Message), which restricts the server to handle 65k Clients at a time, which is a reasonable limit.
       *  client ids will be reused once the counter loops (but we make sure to not reassing an id, which is currently in use).
       */
      uint16_t id;
      std::string remoteIp; // including the port
    };

    /** This will create a Client object from the accepted socket and start listening for data
     *  A reference to the newly created client will be returned
     */
    Client& ProcessAcceptedConnection();

    /** Called by the client object whenever a new message has been fully received.
     */
    void ProcessReceivedMessage(Client& client, MessagePointer message);



    struct AcceptData {
      Resource<SOCKET> socket;
      WSAOVERLAPPED overlapped;
      char buffer[128];
    };


    struct Clients {
      /** Adds a new client to the list and map and returns a reference to it.
       *  This method must only be called from by the network thread.
       */
      Client& Add(Server& server, Resource<SOCKET>&& socket);
      
      /** Removes the client instance from the client list.
       *  This method must only be called from by the network thread.
       */
      void Remove(Client& client);


      /** Deletes all clients
       */
      void Clear(); 

      /** This method will attempt to enqueue a message for the specified client to be sent.
       *  Returns false if the client doesn't exist and the message has been dropped.
       *  This method will mainly be called from within the main thread.
       */
      bool QueueClientMessage(uint16_t clientId, MessagePointer message);
      
      std::list<Client> list; // all connected clients

      /** Map from client id to client. This is used for efficiently sending replies to clients based on their id
       */
      std::unordered_map<uint16_t, Client*> map;

      /** Mutex, which must be acquired by the network thread when modifying the client list and by the main thread
       *  when performing a lookup in the client map to ensure the data structure doesn't change while attempting to schedule a send
       */
      std::mutex mutex;

      /** The last used client id. This is used to assign a unique id to each new client.
       *  This field can and will overflow, but we will ensure to not reassign an id, which is currently in use.
       */
      uint16_t lastId = 0;
    };




    std::thread networkThread;
    const int listenPort;
    Resource<SOCKET> listenSocket;
    IOCompletionPort ioCompletionPort;
    Clients clients; // all connected clients
    std::atomic<bool> running;

    AcceptData accept; // structure containing the necessary fields for the current async accept call

    //TODO: or better one queue per Client?
    ReceiveMessageQueue receiveQueue; // all received messages will be pushed into this queue
};


}
}