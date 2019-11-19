/*
 *
 * Copyright 2019 gRPC authors.
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

#ifndef GRPCPP_TEST_DEFAULT_REACTOR_TEST_PEER_H
#define GRPCPP_TEST_DEFAULT_REACTOR_TEST_PEER_H

#include <grpcpp/server_context.h>
#include <grpcpp/support/server_callback.h>

namespace grpc {
namespace testing {

/// A test-only class to monitor the behavior of the ServerContext's
/// DefaultReactor. It is intended for allow unit-testing of a callback API
/// service via direct invocation of the service methods rather than through
/// RPCs. It is only applicable for unary RPC methods that use the
/// DefaultReactor rather than any user-defined reactor.
class DefaultReactorTestPeer {
 public:
  explicit DefaultReactorTestPeer(experimental::CallbackServerContext* ctx)
      : DefaultReactorTestPeer(ctx, [](::grpc::Status) {}) {}
  DefaultReactorTestPeer(experimental::CallbackServerContext* ctx,
                         std::function<void(::grpc::Status)> finish_func)
      : ctx_(ctx) {
    ctx->SetupTestDefaultReactor(std::move(finish_func));
  }
  ::grpc::experimental::ServerUnaryReactor* reactor() const {
    return &ctx_->default_reactor_;
  }
  bool test_status_set() const { return ctx_->test_status_set(); }
  Status test_status() const { return ctx_->test_status(); }

 private:
  experimental::CallbackServerContext* const ctx_;  // not owned
};

}  // namespace testing
}  // namespace grpc

#endif  // GRPCPP_TEST_DEFAULT_REACTOR_TEST_PEER_H
