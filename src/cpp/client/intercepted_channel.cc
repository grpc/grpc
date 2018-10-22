/*
 *
 * Copyright 2018 gRPC authors.
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

#include <grpcpp/impl/codegen/intercepted_channel.h>

#include <grpcpp/channel.h>

namespace grpc {
namespace internal {

internal::Call InterceptedChannel::CreateCall(const internal::RpcMethod& method,
                                              ClientContext* context,
                                              CompletionQueue* cq) {
  return (dynamic_cast<Channel*>(channel_))
      ->CreateCallInternal(method, context, cq, interceptor_pos_);
}
}  // namespace internal
}  // namespace grpc
