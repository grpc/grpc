#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include <grpcpp/generic/generic_stub.h>
#include <grpcpp/grpcpp.h>

using namespace std;
using namespace grpc;

namespace generic_grpc
{
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
                    &cli_ctx, method, options, send_buf, &recv_buf,
                    [&recv_buf, &done, &mu, &cv](Status s)
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
}
