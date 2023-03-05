#include <pthread.h>

#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/cal_xor.grpc.pb.h"
#else
#include "cal_xor.grpc.pb.h"
#endif

#include "press.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using XorJob::CalXor;
using XorJob::CalXorRequest;
using XorJob::CalXorResponse;

const int pthread_num = 1;

static int res = 0;

class XorClient {
 public:
  XorClient(std::shared_ptr<Channel> channel)
      : stub_(CalXor::NewStub(channel)) {}

  int64_t CalculateXor(const int64_t num1, const int64_t num2) {
    CalXorRequest request;
    request.set_num1(num1);
    request.set_num2(num2);

    CalXorResponse reply;

    ClientContext context;

    Status status = stub_->CalculateXor(&context, request, &reply);

    if (status.ok()) {
      return reply.num();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return -1;
    }
  }

  PressTest press_test_;

 private:
  std::unique_ptr<CalXor::Stub> stub_;
};

static void* worker(void* arg) {
  XorClient* xor_job = static_cast<XorClient*>(arg);
  PressTest* press_test = &xor_job->press_test_;

  while (true) {
    press_test->SetCurrentTime();
    const int64_t num1 = 123;
    const int64_t num2 = 123;
    res = xor_job->CalculateXor(num1, num2);
    if (press_test->GetLantency() == -1) {
      break;
    }
    std::cout << "CalXor received: " << res << std::endl;
  }

  return nullptr;
}

int main(int argc, char** argv) {
  std::string target_str = "localhost:50051";

  XorClient *xor_job[pthread_num];
  for (int i = 0; i < pthread_num; i++) {
    xor_job[i] = new XorClient(grpc::CreateChannel(target_str, 
                            grpc::InsecureChannelCredentials()));
  }
  
  std::vector<pthread_t> pts(pthread_num);

  for (int i = 0; i < pthread_num; i++) {
    if (pthread_create(&pts[i], NULL, worker, xor_job[i]) != 0) {
      std::cout << "Fail to create pthread" << std::endl;
      return -1;
    }
  }

  for (int i = 0; i < pthread_num; i++) {
      pthread_join(pts[i], NULL);
  }

  printf("start to print press result\n");
  PressResult press_result;
  for (int i = 0; i < pthread_num; i++) {
    press_result.CollectResult(&xor_job[i]->press_test_);
  }
  press_result.PrintResult();
  
  return 0;
}
