#include <network/StandaloneFactory.hpp>
#include <network/ReceiveMessageQueue.hpp>
#include <network/IOCPReceiveMessageQueue.hpp>
#include <network/message/All.hpp>

#include <common/Exception.hpp>

#include <vector>
#include <mutex>

namespace blobs::network {

class StandaloneFactory::Client final : public ClientInterface {
public:
  Client(Server& server, client_id id);
  virtual ~Client() override;

  /** Used to send already allocated messages to the server (SendMessage() is sadly already in use by WinAPI)
   */
  virtual void SendMessageToServer(MessagePointer&& message);

  /** Wait for the next sever message without a timeout
   */
  virtual MessagePointer AwaitMessage();


  const client_id id;
  ReceiveMessageQueue messageQueue;
private:
  Server* server;
};


class StandaloneFactory::Server final : public ServerInterface {
public:  
  Server(StandaloneFactory& factory, IOCPReceiveMessageQueue& serverReceiveQueue) : factory(factory), lastClientId(0), messageQueue(serverReceiveQueue) {
    factory.server = this;
  }

  virtual ~Server() override {
    TODO("Notify all clients about the closed connection somehow...");
    factory.server = nullptr;
  }

  /** Wait for the next sever message without a timeout
   */
  virtual MessagePointer FetchMessage() override {
    return messageQueue.FetchMessage();
  }

  /** Send an already allocated message to the specified client
   */
  virtual void SendMessageToClient(client_id clientId, MessagePointer message) {
    std::unique_lock<std::mutex> clientsLock(clientsMutex);
    for (auto client : clients) {
      if (client->id == clientId) {
        client->messageQueue.MessageReceived(std::move(message));
        break;
      }
    }
  }

  /** Used to construct a new standalone client instance connected to this server
   */
  std::unique_ptr<Client> ClientConnected() {
    std::unique_lock<std::mutex> clientsLock(clientsMutex);
    auto client = std::make_unique<Client>(*this, ++lastClientId);
    clients.push_back(client.get());

    // Notify server logic about newly connected client
    messageQueue.MessageReceived(message::ConnectionOpened::Create(client->id, "127.0.0.1"));
    return client;
  }


  void ClientDisconnected(Client* client) {
    std::unique_lock<std::mutex> clientsLock(clientsMutex);
    auto pos = std::find(clients.begin(), clients.end(), client);
    clients.erase(pos);
    clientsLock.unlock();
    
    // Notify server logic about the disconnected client
    messageQueue.MessageReceived(message::ConnectionClosed::Create(client->id));
  }

  IOCPReceiveMessageQueue& messageQueue;
  std::vector<Client*> clients;
  std::mutex clientsMutex;
private:
  client_id lastClientId;
  StandaloneFactory& factory;
};



StandaloneFactory::Client::Client(Server& server, client_id id) : server(&server), id(id) {}

StandaloneFactory::Client::~Client() {
  // Notify server about the client disconnecting
  server->ClientDisconnected(this);
}

void StandaloneFactory::Client::SendMessageToServer(MessagePointer&& message) {
  message->clientId = id;
  server->messageQueue.MessageReceived(std::move(message));
}

MessagePointer StandaloneFactory::Client::AwaitMessage() {
  return messageQueue.AwaitMessage();
}



StandaloneFactory::StandaloneFactory() : server(nullptr) {}

void StandaloneFactory::Use() {
  static StandaloneFactory standaloneFactory;
  SetInstance(&standaloneFactory);
}


std::unique_ptr<ClientInterface> StandaloneFactory::CreateClient(std::string serverAddress, int serverPort) {
  if (!server) {
    throw blobs::Exception("blobs::Initialize() not called or blobs::Shutdown() already called!");
  }

  return server->ClientConnected();
}

std::unique_ptr<ServerInterface> StandaloneFactory::CreateServer(IOCPReceiveMessageQueue& serverReceiveQueue, int listenPort) {
  if (server) {
    throw blobs::Exception("Logic error: Multiple standalone server instances in one process are not allowed!");
  }
  
  return std::make_unique<Server>(*this, serverReceiveQueue);
}




}