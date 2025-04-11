#include "src/core/ext/transport/chttp2/transport/ping_promise.h"

namespace grpc_core {
namespace http2 {
KeepAliveSystem::KeepAliveSystem(
    std::unique_ptr<KeepAliveSystemInterface> keep_alive_interface,
    Duration keepalive_timeout)
    : keep_alive_interface_(std::move(keep_alive_interface)),
      keepalive_timeout_(keepalive_timeout) {}

void KeepAliveSystem::Spawn(Party* party, Duration keepalive_interval) {
  party->Spawn("KeepAlive", Loop([this, keepalive_interval]() {
                 ResetDataReceived();
                 return TrySeq(
                     Race(WaitForData(),
                          TrySeq(Sleep(keepalive_interval),
                                 If(keepalive_timeout_ != Duration::Infinity(),
                                    TimeoutAndSendPing(),
                                    [this] { return SendPing(); }))),
                     [this]() -> LoopCtl<absl::Status> {
                       ResetDataReceived();
                       return Continue();
                     });
               }),
               [](auto status) {
                 LOG(INFO) << "KeepAlive end with status: " << status;
               });
}
}  // namespace http2
}  // namespace grpc_core
