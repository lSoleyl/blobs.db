#pragma once

#include <network/MessagePointer.hpp>
#include "..\blobs\Exception.hpp"

#include <sstream>

namespace blobs {
namespace network { class Client; }

namespace internal {

class Network {
public:
  /** Opens a new client connection and returns a client id, which can be used to reference this client or 
   *  returns the id of an already existing connection if the connection string matches.
   */
  static connection_id Get(std::string_view connectionString);

  /** When a connection retrieved through Get() is no longer needed, Release should be called to close it once
   *  the last user releases it.
   */
  static void Release(connection_id connectionId);

  /** When receiving a close notification from the server, this should be called to properly deinitialize the client connection and thread.
   */
  static void ServerClosedConnection(connection_id connectionId);

  /** Returns the previously initialized client connection instance
   */
  static network::Client& Get(connection_id connectionId);


  /** A wrapper around Client::AwaitMessage, which will automatically translate a NetworkException message into a thrown blobs::Exception
   */
  static network::MessagePointer AwaitMessage(network::Client& connection);

  /** A wrapper around AwaitMessage, which ensures that the returned message is of the correct type and casts it into the given message pointer.
   *  T should obviously be a type derived from network::Message
   */
  template<typename T>
  static network::MessagePointer_T<T> ExpectMessage(network::Client& connection) {
    auto message = AwaitMessage(connection);
    if (!message.Is<T>()) {
      std::ostringstream errorMsg;
      errorMsg << "Expected " << T::type << ", but server replied with " << *message;
      throw blobs::Exception(errorMsg.str());
    }
    return message.Cast<T>();
  }


private:
  /** Creates a new network client from the given connection string
   */
  static std::unique_ptr<network::Client> ClientFromConnectionString(std::string_view connectionString);


  struct Connection {
    std::string connectionString;
    std::unique_ptr<network::Client> client;
    uint32_t useCount; // how many databases are using this connection right now
  };

  /** All currently connected clients (and all previously connected clients)
   *  This vector is not cleared as we assume that we will not constantly change the database servers
   *  and are more likely to resue previously closed connections
   */
  static std::vector<Connection> connections;
};


}}
