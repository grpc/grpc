#include "test/cpp/microbenchmarks/callback_test_service.h"
#include "test/cpp/util/string_ref_helper.h"

namespace grpc {
namespace testing {
namespace {
int GetIntValueFromMetadataHelper(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  if (metadata.find(key) != metadata.end()) {
    std::istringstream iss(ToString(metadata.find(key)->second));
    iss >> default_value;
    // gpr_log(GPR_INFO, "%s : %d", key, default_value);
  }

  return default_value;
}

int GetIntValueFromMetadata(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  return GetIntValueFromMetadataHelper(key, metadata, default_value);
}
} // namespace

void CallbackStreamingTestService::Echo(
    ServerContext* context, const EchoRequest* request, EchoResponse* response,
    experimental::ServerCallbackRpcController* controller) {
  response->set_message(request->message());
  controller->Finish(Status::OK);
}

experimental::ServerBidiReactor<EchoRequest, EchoResponse>*
CallbackStreamingTestService::BidiStream() {
   class Reactor : public experimental::ServerBidiReactor<EchoRequest,
                                                          EchoResponse> {
   public:
    Reactor() {}
    void OnStarted(ServerContext* context) override {
      ctx_ = context;
      server_write_last_ = GetIntValueFromMetadata(
          kServerFinishAfterNReads, context->client_metadata(), 0);
      EchoRequest* request = new EchoRequest;
      request->set_message("");
      request_ = request;
      StartRead(request_);
    }
    void OnDone() override {}
    void OnCancel() override {}
    void OnReadDone(bool ok) override {
      if (ok) {
        num_msgs_read_++;
        // gpr_log(GPR_INFO, "recv msg %s", request_->message().c_str());
        response_->set_message(request_->message());
        if (num_msgs_read_ == server_write_last_) {
          StartWriteLast(response_, WriteOptions());
          // If we use WriteLast, we shouldn't wait before attempting Finish
        } else {
          StartWrite(response_);
          return;
        }
      }
      FinishOnce(Status::OK);
    }
    void OnWriteDone(bool ok) override {
      if (!finished_) {
        StartRead(request_);
      }
    }

   private:
    void FinishOnce(const Status& s) {
      if (!finished_) {
        Finish(s);
        finished_ = true;
      }
    }

    ServerContext* ctx_;
    EchoRequest* request_;
    EchoResponse* response_;
    int num_msgs_read_{0};
    int server_write_last_;
    bool finished_{false};
  };

  return new Reactor;
}
}  // namespace testing
}  // namespace grpc

