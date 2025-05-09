#include <grpcpp/grpcpp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <thread>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

void connection_accepter_thread_function(
    int server_fd,
    std::unique_ptr<grpc::experimental::PassiveListener>& passive_listener) {
  // Listen for incoming connections
  if (listen(server_fd, 128) < 0) {
    std::cerr << "Error listening on server socket" << std::endl;
    close(server_fd);
    return;
  }
  while (true) {
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    std::cout << "Thread: Waiting to accept a new connection on server_fd: "
              << server_fd << std::endl;
    int new_socket_fd =
        accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

    if (new_socket_fd < 0) {
      std::cerr << "Error accepting connection" << std::endl;
      close(server_fd);
      return;
    }

    std::cout << "Thread: Accepted new connection. new_socket_fd: "
              << new_socket_fd << std::endl;
    auto status = passive_listener->AcceptConnectedFd(new_socket_fd);
    if (!status.ok()) {
      std::cout << "gRPC server serving on FD failed " << new_socket_fd
                << std::endl;
    } else {
      std::cout << "gRPC server serving on FD " << new_socket_fd << std::endl;
    }
  }

  if (server_fd >= 0) {
    close(server_fd);
  }
}

class GreeterServiceImpl final : public Greeter::Service {
  Status SayHello(ServerContext* context, const HelloRequest* request,
                  HelloReply* reply) override {
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return Status::OK;
  }
};

int main(int argc, char** argv) {
  // Create a listening socket
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    std::cerr << "Error creating server socket" << std::endl;
    return 1;
  }

  // Bind the socket to an address and port
  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
  server_addr.sin_port = htons(50051);
  if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
      0) {
    std::cerr << "Error binding server socket" << std::endl;
    close(server_fd);
    return 1;
  }

  std::cout << "Server listening on port 50051..." << std::endl;

  std::unique_ptr<grpc::experimental::PassiveListener> passive_listener;
  ServerBuilder builder;
  GreeterServiceImpl service;
  builder.RegisterService(&service);
  auto server = builder.experimental()
                    .AddPassiveListener(grpc::InsecureServerCredentials(),
                                        passive_listener)
                    .BuildAndStart();
  // Accept an incoming connection

  std::thread th(connection_accepter_thread_function, server_fd,
                 std::ref(passive_listener));
  th.detach();
  server->Wait();
  // server->Shutdown();

  return 0;
}