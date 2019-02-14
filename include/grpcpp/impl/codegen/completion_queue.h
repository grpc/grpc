/*
 *
 * Copyright 2015-2016 gRPC authors.
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

/// A completion queue implements a concurrent producer-consumer queue, with
/// two main API-exposed methods: \a Next and \a AsyncNext. These
/// methods are the essential component of the gRPC C++ asynchronous API.
/// There is also a \a Shutdown method to indicate that a given completion queue
/// will no longer have regular events. This must be called before the
/// completion queue is destroyed.
/// All completion queue APIs are thread-safe and may be used concurrently with
/// any other completion queue API invocation; it is acceptable to have
/// multiple threads calling \a Next or \a AsyncNext on the same or different
/// completion queues, or to call these methods concurrently with a \a Shutdown
/// elsewhere.
/// \remark{All other API calls on completion queue should be completed before
/// a completion queue destructor is called.}
#ifndef GRPCPP_IMPL_CODEGEN_COMPLETION_QUEUE_H
#define GRPCPP_IMPL_CODEGEN_COMPLETION_QUEUE_H

#include <grpcpp/impl/codegen/completion_queue_impl.h>

namespace grpc {

typedef ::grpc_impl::CompletionQueue CompletionQueue;
typedef ::grpc_impl::ServerCompletionQueue ServerCompletionQueue;

}  // namespace grpc

#endif  // GRPCPP_IMPL_CODEGEN_COMPLETION_QUEUE_H
