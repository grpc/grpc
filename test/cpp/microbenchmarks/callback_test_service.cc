#include "test/cpp/microbenchmarks/callback_test_service.h"

namespace grpc {
namespace testing {
namespace {

grpc::string ToString(const grpc::string_ref& r) {
  return grpc::string(r.data(), r.size());
}

int GetIntValueFromMetadataHelper(
    const char* key,
    const std::multimap<grpc::string_ref, grpc::string_ref>& metadata,
    int default_value) {
  if (metadata.find(key) != metadata.end()) {
    std::istringstream iss(ToString(metadata.find(key)->second));
    iss >> default_value;
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
      message_size_ = GetIntValueFromMetadata(
          kServerResponseStreamsToSend, context->client_metadata(), 0);
//      EchoRequest* request = new EchoRequest;
//      if (message_size_ > 0) {
//        request->set_message(std::string(message_size_, 'a'));
//      } else {
//        request->set_message("");
//      }
//
//      request_ = request;
      StartRead(&request_);
      on_started_done_ = true;
    }
    void OnDone() override { delete this; }
    void OnCancel() override {}
    void OnReadDone(bool ok) override {
      if (ok) {
        num_msgs_read_++;
//        gpr_log(GPR_INFO, "recv msg %s", request_.message().c_str());
        if (message_size_ > 0) {
          response_.set_message(std::string(message_size_, 'a'));
        } else {
          response_.set_message("");
        }
        if (num_msgs_read_ == server_write_last_) {
          StartWriteLast(&response_, WriteOptions());
        } else {
          StartWrite(&response_);
          return;
        }
      }
      FinishOnce(Status::OK);
    }
    void OnWriteDone(bool ok) override {
      std::lock_guard<std::mutex> l(finish_mu_);
      if (!finished_) {
        StartRead(&request_);
      }
    }

   private:
    void FinishOnce(const Status& s) {
      std::lock_guard<std::mutex> l(finish_mu_);
      if (!finished_) {
        Finish(s);
        finished_ = true;
      }
    }

    ServerContext* ctx_;
    EchoRequest request_;
    EchoResponse response_;
    int num_msgs_read_{0};
    int server_write_last_;
    int message_size_;
    std::mutex finish_mu_;
    bool finished_{false};
    bool on_started_done_{false};
  };

  return new Reactor;
}
}  // namespace testing
}  // namespace grpc

