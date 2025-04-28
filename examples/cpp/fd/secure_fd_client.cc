#include <arpa/inet.h>
#include <grpcpp/grpcpp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

int main(int argc, char** argv) {
  // 1. Create a socket
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd == -1) {
    std::cerr << "Error creating socket" << std::endl;
    return 1;
  }

  // 2. Connect to the server using the socket
  sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(50051);
  if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address/address not supported" << std::endl;
    close(client_fd);
    return 1;
  }

  if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
      0) {
    std::cerr << "Error connecting to server" << std::endl;
    close(client_fd);
    return 1;
  }

  std::cout << "connecting to server with client_fd" << client_fd << std::endl;
  // 3. Create a gRPC channel from the existing file descriptor
  grpc::ChannelArguments args;
  std::shared_ptr<Channel> channel =
      grpc::experimental::CreateChannelFromFd(client_fd, grpc::InsecureChannelCredentials(), args);

  // 4. Create a stub (client proxy)
  std::unique_ptr<Greeter::Stub> stub = Greeter::NewStub(channel);

  // 5. Perform gRPC call
  HelloRequest request;
  request.set_name("you");
  HelloReply reply;
  ClientContext context;
  Status status = stub->SayHello(&context, request, &reply);

  if (status.ok()) {
    std::cout << "Greeting: " << reply.message() << std::endl;
  } else {
    std::cerr << status.error_code() << ": " << status.error_message()
              << std::endl;
  }
  return 0;
}