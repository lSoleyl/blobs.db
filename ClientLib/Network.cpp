#include "pch.hpp"
#include <internal/Network.hpp>
#include <network/Client.hpp>
#include <network/message/NetworkException.hpp>
#include <blobs/Exception.hpp>


namespace blobs {
namespace internal {

std::vector<Network::Connection> Network::connections;


connection_id Network::Get(std::string_view host, int port) {
  if (port > std::numeric_limits<uint16_t>::max()) {
    throw Exception("Specified connection port too large in '" + std::string(host) + ":" + std::to_string(port) + "'");
  }

  // First check for an existing client
  for (auto& entry : connections) {
    if (entry.host == host && entry.port == port) {
      if (!entry.client) {
        // Connection has been closed -> reopen it
        entry.client = std::make_unique<network::Client>(std::string(host), std::to_string(port));
      }

      // Return the index into the vector as the client id
      ++entry.useCount;
      return static_cast<connection_id>(&entry - connections.data());
    }
  }

  if (connections.size() == std::numeric_limits<connection_id>::max() - 1) {
    // Should this happen -> recompile with larger connection_id type
    throw Exception("Maximum number of server connections reached");
  }


  // No connection to that server made yet -> create a new entry
  connections.push_back({ std::string(host), port, std::make_unique<network::Client>(std::string(host), std::to_string(port)), 1 });
  return static_cast<connection_id>(connections.size() - 1);
}

void Network::Release(connection_id connectionId) {
  assert(connectionId < connections.size() && connections[connectionId].useCount > 0);
  if (--connections[connectionId].useCount == 0) {
    // Last user released the connection -> close it
    connections[connectionId].client.reset();
  }
}


void Network::ServerClosedConnection(connection_id connectionId) {
  assert(connectionId < connections.size());
  --connections[connectionId].useCount;
  connections[connectionId].client.reset(); // always reset the connection if the server closed it
}




network::Client& Network::Get(connection_id connectionId) {
  assert(connectionId < connections.size());
  if (!connections[connectionId].client) {
    // Database attempting to access an already closed connection
    throw Exception("Server connection already closed");
  }
  return *connections[connectionId].client;
}

network::MessagePointer Network::AwaitMessage(network::Client& connection) {
  auto message = connection.AwaitMessage();
  if (auto ex = message.Get<network::message::NetworkException>()) {
    // Translate any network exception into a blobs::Exception
    throw blobs::Exception(ex->GetExceptionMessage());
  }
  return message;
}


}}

