#include <network/message/All.hpp>

#include <sstream>

namespace blobs {
namespace network {
namespace message {


std::ostream& operator<<(std::ostream& out, Type type) {
  switch (type) {
    case Type::DatabaseOpen: out << "DatabaseOpen"; break;
    case Type::DatabaseOpenResponse: out << "DatabaseOpenResponse"; break;
    case Type::DatabaseClose: out << "DatabaseClose"; break;

    case Type::BlobsRead: out << "BlobsRead"; break;
    case Type::BlobsReadResponse: out << "BlobsReadResponse"; break;

    case Type::TransactionBegin: out << "TransactionBegin"; break;
    case Type::TransactionAbort: out << "TransactionAbort"; break;
    case Type::TransactionCommit: out << "TransactionCommit"; break;
    case Type::TransactionSetMode: out << "TransactionSetMode"; break;

    case Type::ConnectionOpened: out << "ConnectionOpened"; break;
    case Type::ConnectionClosed: out << "ConnectionClosed"; break;
    case Type::NetworkException: out << "NetworkException"; break;

    default: out << "???"; break;
  }
  return out;
}

Message::Message(message_size size, Type type) : size(size), type(type), clientId(0) {}



std::string Message::ToString() const {
  std::ostringstream out;
  out << *this;
  return out.str();
}

std::ostream& operator<<(std::ostream& out, const Message& message) {
  // Some message types have their own stringification. Due to a lack of virtual methods we have to dispatch them manually
  switch (message.type) {
    case Type::DatabaseOpen: return out << static_cast<const DatabaseOpen&>(message);
    case Type::DatabaseOpenResponse: return out << static_cast<const DatabaseOpenResponse&>(message);
    case Type::DatabaseClose: return out << static_cast<const DatabaseClose&>(message);

    case Type::BlobsRead: return out << static_cast<const BlobsRead&>(message);

    case Type::ConnectionOpened: return out << static_cast<const ConnectionOpened&>(message);
    case Type::NetworkException: return out << static_cast<const NetworkException&>(message);

    
    default: 
      // Default stringification simply writes the message type
      return out << message.type; 
  }
}


}}}
