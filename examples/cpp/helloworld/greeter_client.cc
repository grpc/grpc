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

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/generic/generic_stub.h>

// #include "proto_helper.h"

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");

using namespace std;
using namespace grpc;
// using grpc::Channel;
// using grpc::ClientContext;
// using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

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
    
private:
    std::unique_ptr<Greeter::Stub> stub_;
};

std::unique_ptr<grpc::ByteBuffer> SerializeToByteBuffer(std::string& message) {
    Slice slice(message);
    return std::make_unique<grpc::ByteBuffer>(&slice, 1);
}

void ByteBufferToString(ByteBuffer* buffer, std::string& str)
{
    std::vector<Slice> slices;
    (void)buffer->Dump(&slices);
    str.reserve(buffer->Length());
    for (auto s = slices.begin(); s != slices.end(); s++)
    {
        str.append(reinterpret_cast<const char*>(s->begin()), s->size());
    }
}

void RPC(const std::string target_str, const std::string method, const grpc::ByteBuffer* send_buf)
{
    auto channel = grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials());
    auto stub = std::make_unique<grpc::GenericStub>(channel);
    grpc::ClientContext cli_ctx;
    
    grpc::StubOptions options("grpc_stats");
    grpc::ByteBuffer recv_buf;
    
    bool done = false;
    std::mutex mu;
    std::condition_variable cv;
    
    stub->UnaryCall(
                    &cli_ctx, method, options, send_buf, &recv_buf, [&recv_buf, &done, &mu, &cv](Status s)
                    {
                        if (!s.ok())
                        {
                            std::cout << "Something is wrong" << endl;
                            done = true;
                            return;
                        }
                        
                        std::lock_guard<std::mutex> l(mu);
                        done = true;
                        cv.notify_one();
                        
                        std::string response;
                        ByteBufferToString(&recv_buf, response);
                        
                        std::cout << "Response:" << response << endl;
                    });
    std::unique_lock<std::mutex> l(mu);
    while (!done) {
        cv.wait(l);
    }
}

int main(int argc, char** argv) {
    absl::ParseCommandLine(argc, argv);
    std::string target_str = absl::GetFlag(FLAGS_target);
    std::string kMethodName("/helloworld.Greeter/SayHello");
    std::string data("\n\U00000004John");
    grpc::ByteBuffer* send_buf;
    send_buf = SerializeToByteBuffer(data).get();

    RPC(target_str, kMethodName, send_buf);

    return 0;
}
