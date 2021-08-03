/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

//my include
#include <grpc/support/port_platform.h>
//#include "src/core/lib/security/credentials/tls/grpc_tls_certificate_provider.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <openssl/ssl.h>
//#include "src/core/lib/gprpp/stat.h"
//#include "src/core/lib/slice/slice_internal.h"
//#include "src/core/lib/surface/api_trace.h"

#define CA_CERT_PATH "src/core/tsi/test_creds/ca.pem"
#define CLIENT_CERT_PATH "src/core/tsi/test_creds/client1.pem"
#define CLIENT_KEY_PATH "src/core/tsi/test_creds/client1.key"
//done

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
using ::grpc::experimental::TlsServerAuthorizationCheckArg;
using ::grpc::experimental::TlsServerAuthorizationCheckConfig;
using ::grpc::experimental::TlsServerAuthorizationCheckInterface;

class TestTlsServerAuthorizationCheck
    : public TlsServerAuthorizationCheckInterface {
  int Schedule(TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    std::string cb_user_data = "cb_user_data";
    arg->set_cb_user_data(static_cast<void*>(gpr_strdup(cb_user_data.c_str())));
    arg->set_success(1);
    arg->set_target_name("sync_target_name");
    arg->set_peer_cert("sync_peer_cert");
    arg->set_status(GRPC_STATUS_OK);
    arg->set_error_details("sync_error_details");
    return 1;
  }

  void Cancel(TlsServerAuthorizationCheckArg* arg) override {
    GPR_ASSERT(arg != nullptr);
    arg->set_status(GRPC_STATUS_PERMISSION_DENIED);
    arg->set_error_details("cancelled");
  }
};


class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string SayHello(const std::string& user) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(user);

    // Container for the data we expect from the server.
    HelloReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->SayHello(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
      return reply.message();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

  std::string SayHelloAgain(const std::string& user) {
    // Follows the same pattern as SayHello.
    HelloRequest request;
    request.set_name(user);
    HelloReply reply;
    ClientContext context;

    // Here we can use the stub's newly available method we just added.
    Status status = stub_->SayHelloAgain(&context, request, &reply);
    if (status.ok()) {
      return reply.message();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

// a simple exact match authorization class.
//class ExampleSyncTlsAuthorization : public TlsAuthorizationCheckInterface {
//  int Schedule(TlsAuthorizationCheckArg* arg) override {
//    std::string target_name = arg->target_name();
//    std::string peer_cert = arg->peer_cert();
//    std::string spiffe_id = arg->spiffe_id();
//    if (spiffe_id == "spiffe://foo.bar.com/workload/id/1") {
//        arg->set_success(1);
//        arg->set_status(GRPC_STATUS_OK);
//    } else {
//        arg->set_success(0);
//        arg->set_status(GRPC_STATUS_CANCELLED);
//    }
//    /** Return zero to indicate that authorization was done synchronously. **/
//    return 0;
//  }
//};

int main(int argc, char** argv) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  // We indicate that the channel isn't authenticated (use of
  // InsecureChannelCredentials()).
  std::string target_str;
  std::string arg_str("--target");
  if (argc > 1) {
    std::string arg_val = argv[1];
    size_t start_pos = arg_val.find(arg_str);
    if (start_pos != std::string::npos) {
      start_pos += arg_str.size();
      if (arg_val[start_pos] == '=') {
        target_str = arg_val.substr(start_pos + 1);
      } else {
        std::cout << "The only correct argument syntax is --target="
                  << std::endl;
        return 0;
      }
    } else {
      std::cout << "The only acceptable argument is --target=" << std::endl;
      return 0;
    }
  } else {
    target_str = "localhost:50051";
  }
  constexpr const char* kCertName = "cert_name";
  constexpr const char* kRootCertName = "root_cert_name";
  constexpr const char* kIdentityCertName = "identity_cert_name";
//  auto certificate_provider = std::make_shared<grpc::experimental::FileWatcherCertificateProvider>(
//    CLIENT_KEY_PATH, CLIENT_CERT_PATH, CA_CERT_PATH, 1);
  grpc::experimental::TlsChannelCredentialsOptions options;
  options.watch_root_certs();
  options.set_root_cert_name(kRootCertName);
  options.watch_identity_key_cert_pairs();
  options.set_identity_cert_name(kIdentityCertName);
  options.set_server_verification_option(GRPC_TLS_SERVER_VERIFICATION);
  auto test_server_authorization_check = std::make_shared<TestTlsServerAuthorizationCheck>();
  auto server_authorization_check_config = std::make_shared<TlsServerAuthorizationCheckConfig>(test_server_authorization_check);
  options.set_server_authorization_check_config(server_authorization_check_config);
  auto channel_credentials = grpc::experimental::TlsCredentials(options);
  std::shared_ptr<grpc::Channel> channel = CreateChannel("0.0.0.0:50051", channel_credentials);
  std::unique_ptr<Greeter::Stub> stub(Greeter::NewStub(channel));
  GreeterClient greeter(
      grpc::CreateChannel(target_str, channel_credentials));
//      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));


  std::string user("world");
  std::string reply = greeter.SayHello(user);
  std::cout << "Greeter received: " << reply << std::endl;

  reply = greeter.SayHelloAgain(user);
  std::cout << "Greeter received: " << reply << std::endl;

  return 0;
}
