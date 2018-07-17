#include <mutex>
#include <thread>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/resource_quota.h>
#include <grpcpp/security/auth_metadata_processor.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

#include "src/core/lib/gpr/env.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/proto/grpc/testing/duplicate/echo_duplicate.grpc.pb.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_service_impl.h"
#include "test/cpp/util/string_ref_helper.h"
#include "test/cpp/util/test_credentials_provider.h"

#include <gtest/gtest.h>
#include "src/proto/grpc/testing/repro.grpc.pb.h"

namespace rxrpc
{
namespace test
{
    class AccumulationService : public ::test::BasicTestService::Service
    {
    public:
        ::grpc::Status Accumulate(
            ::grpc::ServerContext *context,
            ::grpc::ServerReader<::test::TestRequest> *reader,
            ::test::TestResponse *response) override
        {
            ::test::TestRequest request;
            int accumulatedValue = 0;
            int accumulatedEntries = 0;
            while (reader->Read(&request) && accumulatedEntries < m_requiredRequests)
            {
                accumulatedValue += request.value();
                accumulatedEntries++;
            }

            response->set_value(accumulatedValue);
            gpr_log(GPR_ERROR, "SERVER IS DONE PROCESSING");
            return ::grpc::Status(::grpc::StatusCode::OK, "");
        }

    public:
        const int m_requiredRequests{2};
    };

    TEST(GRPCClientStreamingTest, simple_test)
    {
        // create a synchronous server
        AccumulationService service;
        ::grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:50000", ::grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        auto server = builder.BuildAndStart();
        auto serverThread = std::thread([&server]() { server->Wait(); });

        // create a synchronous stub
        auto channel = grpc::CreateChannel("localhost:50000", ::grpc::InsecureChannelCredentials());
        ::test::BasicTestService::Stub stub(channel);

        // start the stream
        ::grpc::ClientContext context;
        ::test::TestResponse response;
        auto writer = stub.Accumulate(&context, &response);

        // write values into the stream
        bool successful = true;
        int value = 1;
        int numWrites = 0;
        std::string payload(10000, 'a');
        do
        {
            ::test::TestRequest request;
            request.set_value(value++);
            request.set_payload(payload);
            successful = writer->Write(request, ::grpc::WriteOptions{});
            numWrites++;

            // NOTE: successful is never false, indicating a closed RPC
            // client must continue writing infinitely
            if (numWrites > 1000000)
            {
                EXPECT_TRUE(false);
                break;
            }

        } while (successful);

        EXPECT_EQ(response.value(), 15);

        server->Shutdown();
        serverThread.join();
    }
} // namespace test
} // namespace rxrpc


int main(int argc, char** argv) {
  gpr_setenv("GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS", "200");
  grpc_test_init(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
