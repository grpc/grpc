#include "test/core/end2end/fuzzers/fuzzing_common.h"

#include "absl/strings/str_cat.h"

#include <grpc/grpc.h>

#include "src/core/lib/gprpp/crash.h"
#include "test/core/end2end/fuzzers/api_fuzzer.pb.h"

namespace grpc {
namespace testing {

BasicApiFuzzer::Result BasicApiFuzzer::ExecuteAction(
    const api_fuzzer::Action& action) {
  switch (action.type_case()) {
    case api_fuzzer::Action::TYPE_NOT_SET:
      return BasicApiFuzzer::Result::kFailed;
    // tickle completion queue
    case api_fuzzer::Action::kPollCq:
      return PollCq();
    // create an insecure channel
    case api_fuzzer::Action::kCreateChannel:
      return CreateChannel(action.create_channel());
    // destroy a channel
    case api_fuzzer::Action::kCloseChannel:
      return CloseChannel();
    // bring up a server
    case api_fuzzer::Action::kCreateServer:
      return CreateServer(action.create_server());
    // begin server shutdown
    case api_fuzzer::Action::kShutdownServer:
      return ShutdownServer();
    // cancel all calls if server is shutdown
    case api_fuzzer::Action::kCancelAllCallsIfShutdown:
      return CancelAllCallsIfShutdown();
    // destroy server
    case api_fuzzer::Action::kDestroyServerIfReady:
      return DestroyServerIfReady();
    // check connectivity
    case api_fuzzer::Action::kCheckConnectivity:
      return CheckConnectivity(action.check_connectivity());
    // watch connectivity
    case api_fuzzer::Action::kWatchConnectivity:
      return WatchConnectivity(action.watch_connectivity());
    // create a call
    case api_fuzzer::Action::kCreateCall:
      return CreateCall(action.create_call());
    // switch the 'current' call
    case api_fuzzer::Action::kChangeActiveCall:
      return ChangeActiveCall();
    // queue some ops on a call
    case api_fuzzer::Action::kQueueBatch:
      return QueueBatchForActiveCall(action.queue_batch());
    // cancel current call
    case api_fuzzer::Action::kCancelCall:
      return CancelActiveCall();
    // get a calls peer
    case api_fuzzer::Action::kGetPeer:
      return ValidatePeerForActiveCall();
    // get a channels target
    case api_fuzzer::Action::kGetTarget:
      return ValidateChannelTarget();
    // send a ping on a channel
    case api_fuzzer::Action::kPing:
      return SendPingOnChannel();
    // enable a tracer
    case api_fuzzer::Action::kEnableTracer: {
      grpc_tracer_set_enabled(action.enable_tracer().c_str(), 1);
      break;
    }
    // disable a tracer
    case api_fuzzer::Action::kDisableTracer: {
      grpc_tracer_set_enabled(action.disable_tracer().c_str(), 0);
      break;
    }
    // request a server call
    case api_fuzzer::Action::kRequestCall:
      return ServerRequestCall();
    // destroy a call
    case api_fuzzer::Action::kDestroyCall:
      return DestroyActiveCall();
    // resize the buffer pool
    case api_fuzzer::Action::kResizeResourceQuota:
      return ResizeResourceQuota(action.resize_resource_quota());
    default:
      grpc_core::Crash(absl::StrCat("Unsupported Fuzzing Action of type: ",
                                    action.type_case()));
  }
  return BasicApiFuzzer::Result::kComplete;
}

}  // namespace testing
}  // namespace grpc
