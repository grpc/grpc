#ifndef TEST_CPP_CALLBACKMICROBENCHMARKS_CALLBACK_TEST_SERVICE_H
#define TEST_CPP_CALLBACKMICROBENCHMARKS_CALLBACK_TEST_SERVICE_H

#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include <sstream>

namespace grpc {
namespace testing {

const char* const kServerFinishAfterNReads = "server_finish_after_n_reads";

class CallbackStreamingTestService
    : public EchoTestService::ExperimentalCallbackService {
 public:
  CallbackStreamingTestService() {}
  void Echo(ServerContext* context, const EchoRequest* request,
            EchoResponse* response,
            experimental::ServerCallbackRpcController* controller) override;

//   experimental::ServerReadReactor<EchoRequest, EchoResponse>* RequestStream()
//       override;
//
//   experimental::ServerWriteReactor<EchoRequest, EchoResponse>* ResponseStream()
//       override;
  experimental::ServerBidiReactor<EchoRequest, EchoResponse>* BidiStream() override;
};

class BidiClient
    : public grpc::experimental::ClientBidiReactor<EchoRequest, EchoResponse> {
 public:
  BidiClient(EchoTestService::Stub* stub, EchoRequest* request,
      EchoResponse* response, ClientContext* context, int num_msgs_to_send)
      : request_{request},
        response_{response},
        context_{context},
        msgs_to_send_{num_msgs_to_send}{
    stub->experimental_async()->BidiStream(context_, this);
    MaybeWrite();
    StartRead(response_);
    StartCall();
  }

  void OnReadDone(bool ok) override {
    if (ok && reads_complete_ < msgs_to_send_) {
      reads_complete_++;
      StartRead(response_);
    }
  }

  void OnWriteDone(bool ok) override {
    if (!ok) {
      return;
    }
    writes_complete_++;
    MaybeWrite();
  }

  void OnDone(const Status& s) override {
    // LOG(ERROR) << " send message " << writes_complete_;
    // LOG(ERROR) << " read message " << reads_complete_;
    // if (!s.ok()){
    //   LOG(ERROR) << " status error " << s.error_message();
    // }
  }

 private:
  void MaybeWrite() {
    if (writes_complete_ == msgs_to_send_) {
      StartWritesDone();
    } else {
      StartWrite(request_);
    }
  }
  EchoRequest* request_;
  EchoResponse* response_;
  ClientContext* context_;
  int reads_complete_{0};
  int writes_complete_{0};
  const int msgs_to_send_;
};


}  // namespace testing
}  // namespace grpc
#endif  // TEST_CPP_CALLBACKMICROBENCHMARKS_CALLBACK_TEST_SERVICE_H
