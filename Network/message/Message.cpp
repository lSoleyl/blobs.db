#include <network/message/Message.hpp>

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
  // For now just stringify the message type... later maybe implement a ToString() in each message class and cast there based on the type
  out << message.type;
  return out;
}


}}}
