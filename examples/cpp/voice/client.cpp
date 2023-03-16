#include <grpcpp/grpcpp.h>
#include <google/protobuf/arena.h>
#include "roomServer.grpc.pb.h"
#include "interceptors/TelemetryInterceptorFactory.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::RoomServer;
using grpc::RoomSessionMessage;
using grpc::RoomFeedsResponse;

constexpr char GRPC_SERVER_EP[] = "10.164.0.157:5100";

class VoiceChatGrpcClient {
 public:
  VoiceChatGrpcClient()
      : stub_(RoomServer::NewStub(
            grpc::CreateChannel(
                GRPC_SERVER_EP,
                grpc::InsecureChannelCredentials()))) {}

  VoiceChatGrpcClient(std::shared_ptr<grpc::Channel> channel)
      : stub_(RoomServer::NewStub(channel)) {}

  void test() {
    // Unary rpc call.
    auto reply = asyncRpc();
    reply = asyncRpc();
    reply = asyncRpc();
    reply = asyncRpc();
    reply = asyncRpc();
    std::cout << "result: " << reply << std::endl;
  }

  std::string unaryRpc() {
    grpc::ChangeNotificationRateRequest* req = google::protobuf::Arena::CreateMessage<grpc::ChangeNotificationRateRequest>(&arena);
    req->set_notificationspersecond(1);
    grpc::ChangeNotificationRateResponse* reply = google::protobuf::Arena::CreateMessage<grpc::ChangeNotificationRateResponse>(&arena);
    ClientContext context;
    auto status = stub_->ChangeNotificationRate(&context, *req, reply);
    if (status.ok()) {
        std::cout << "rpc done:" << reply->ShortDebugString() << std::endl;
    } else {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
    }
    std::cout << "Arena space allocated: " << arena.SpaceAllocated() << std::endl;
    std::cout << "Arena space used: " << arena.SpaceUsed() << std::endl;
    return reply->success() ? "Succeeded! :)" : "Failed! :(";
  }

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string streamingRpc() {
    grpc::UserNotificationStreamRequest* req = google::protobuf::Arena::CreateMessage<grpc::UserNotificationStreamRequest>(&arena);
    req->set_place_id(123);

    grpc::UserNotification* resp = google::protobuf::Arena::CreateMessage<grpc::UserNotification>(&arena);

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    std::unique_ptr<grpc::ClientReader<grpc::UserNotification>> reader(
      stub_->UserNotificationStream(&context, *req));
    int count = 0;
    int last_timestamp = time(nullptr);
    while (reader->Read(resp)) {
      std::cout << "Found notification user_id"
                << resp->user_id() << " json: "
                << resp->json() << std::endl;
      auto elapsed_sec = std::max(time(nullptr) - last_timestamp, 1L);
      last_timestamp = time(nullptr);
      if (count++ % 100 == 0) {
         std::cout << "got " << 100 / elapsed_sec << " msgs per second" << std::endl;
      }
      std::cout << "Arena space allocated: " << arena.SpaceAllocated() << std::endl;
      std::cout << "Arena space used: " << arena.SpaceUsed() << std::endl;
    }
    Status status = reader->Finish();

    // Act upon its status.
    if (status.ok()) {
      return "RPC done";
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

  std::string asyncRpc() {
    grpc::ChangeNotificationRateRequest* req = google::protobuf::Arena::CreateMessage<grpc::ChangeNotificationRateRequest>(&arena);
    req->set_notificationspersecond(1);
    grpc::ChangeNotificationRateResponse* reply = google::protobuf::Arena::CreateMessage<grpc::ChangeNotificationRateResponse>(&arena);
    ClientContext context;

    grpc::CompletionQueue cq;
    std::unique_ptr<grpc::ClientAsyncResponseReader<grpc::ChangeNotificationRateResponse>> rpc(
      stub_->AsyncChangeNotificationRate(&context, *req, &cq));

    Status status;
    rpc->Finish(reply, &status, (void*)1);

    void* got_tag;
    bool ok = false;
    cq.Next(&got_tag, &ok);
    std::cout << "Arena space allocated: " << arena.SpaceAllocated() << std::endl;
    std::cout << "Arena space used: " << arena.SpaceUsed() << std::endl;
    if (ok && got_tag == (void*)1) {
      return reply->success() ? "Succeeded! :)" : "Failed! :(";
    } else {
      return "RPC failed";
    }
  }

 private:
  std::unique_ptr<RoomServer::Stub> stub_;
  google::protobuf::Arena arena;
};

int main()
{
    std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>> interceptorFactories;
    interceptorFactories.push_back(std::make_unique<TelemetryInterceptorFactory>());

    std::shared_ptr<grpc::Channel> channel = grpc::experimental::CreateCustomChannelWithInterceptors(GRPC_SERVER_EP, grpc::InsecureChannelCredentials(), grpc::ChannelArguments(), std::move(interceptorFactories));
    channel->WaitForConnected(gpr_time_add(
              gpr_now(GPR_CLOCK_REALTIME),
              gpr_time_from_seconds(600, GPR_TIMESPAN)));
    std::cout << "channel connected!" << std::endl;
    VoiceChatGrpcClient client(channel);
    std::cout << "client connected!" << std::endl;
    client.test();
    return 0;
}