#pragma once

#include <network/MessagePointer.hpp>
#include "..\blobs\Exception.hpp"

#include <sstream>

namespace blobs {
namespace network { class ClientInterface; }

namespace internal {

class Network {
public:
  Network();
  ~Network();

  /** Opens a new client connection and returns a client id, which can be used to reference this client or 
   *  returns the id of an already existing connection if the connection parameters match.
   */
  connection_id Get(std::string_view host, int port);

  /** When a connection retrieved through Get(connectionString) is no longer needed, Release should be called to close it once
   *  the last user releases it.
   */
  void Release(connection_id connectionId);

  /** When receiving a close notification from the server, this should be called to properly deinitialize the client connection and thread.
   */
  void ServerClosedConnection(connection_id connectionId);

  /** Returns the previously initialized client connection instance
   */
  network::ClientInterface& Get(connection_id connectionId);


  /** A wrapper around Client::AwaitMessage, which will automatically translate a NetworkException message into a thrown blobs::Exception
   */
  network::MessagePointer AwaitMessage(network::ClientInterface& connection);

  /** A wrapper around AwaitMessage, which ensures that the returned message is of the correct type and casts it into the given message pointer.
   *  T should obviously be a type derived from network::Message
   */
  template<typename T>
  network::MessagePointer_T<T> ExpectMessage(network::ClientInterface& connection) {
    auto message = AwaitMessage(connection);
    if (!message.Is<T>()) {
      std::ostringstream errorMsg;
      errorMsg << "Expected " << T::type << ", but server replied with " << *message;
      throw blobs::Exception(errorMsg.str());
    }
    return message.Cast<T>();
  }


private:

  struct Connection {
    std::string host;
    int port;
    std::unique_ptr<network::ClientInterface> client;
    uint32_t useCount; // how many databases are using this connection right now
  };

  /** All currently connected clients (and all previously connected clients)
   *  This vector is not cleared as we assume that we will not constantly change the database servers
   *  and are more likely to resue previously closed connections
   */
  std::vector<Connection> connections;


  // Type is neither copyable not movable
  Network(const Network&) = delete;
  Network(Network&&) = delete;
  Network& operator=(const Network&) = delete;
  Network& operator=(Network&&) = delete;
};


}}
