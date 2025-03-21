#pragma once

#include "..\win_include.hpp"

#include <thread>


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

    std::thread listenThread;
    const int listenPort;
    SOCKET listenSocket;
};


}
}