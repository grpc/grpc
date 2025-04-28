#include <grpcpp/grpcpp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>

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

class GreeterFdServiceImpl final : public Greeter::Service {
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
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(50051);
  if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
      0) {
    std::cerr << "Error binding server socket" << std::endl;
    close(server_fd);
    return 1;
  }

  // Listen for incoming connections
  if (listen(server_fd, 128) < 0) {
    std::cerr << "Error listening on server socket" << std::endl;
    close(server_fd);
    return 1;
  }

  std::cout << "Server listening on port 50051..." << std::endl;

  // Accept an incoming connection
  sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int new_socket_fd =
      accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
  if (new_socket_fd < 0) {
    std::cerr << "Error accepting connection" << std::endl;
    close(server_fd);
    return 1;
  }
  close(server_fd);  // No longer need the listening socket

  // Create a gRPC server using the accepted file descriptor
  ServerBuilder builder;
  GreeterFdServiceImpl service;
  std::unique_ptr<Server> server(builder.BuildAndStart());
  builder.RegisterService(&service);

  // Add the accepted FD to the server
#ifdef GPR_SUPPORT_CHANNELS_FROM_FD
  grpc::AddInsecureChannelFromFd(server.get(), new_socket_fd);
  std::cout << "gRPC server serving on FD " << new_socket_fd << std::endl;
#else
  std::cerr << "gRPC library not built with GPR_SUPPORT_CHANNELS_FROM_FD"
            << std::endl;
  server->Shutdown();
  return 1;
#endif
  server->Wait();
  return 0;
}