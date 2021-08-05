// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_H

#include <grpc/impl/codegen/port_platform.h>

#include <functional>
#include <string>
#include <vector>

#include "src/core/ext/transport/binder/wire_format/transaction.h"

namespace grpc_binder {

typedef int StreamIdentifier;

class TransportStreamReceiver {
 public:
  virtual ~TransportStreamReceiver() = default;

  // Only handles single time invocation. Callback object will be deleted.
  // The callback should be valid until invocation or unregister.
  virtual void RegisterRecvInitialMetadata(
      StreamIdentifier id, std::function<void(const Metadata&)> cb) = 0;
  // TODO(mingcl): Use string_view
  virtual void RegisterRecvMessage(
      StreamIdentifier id, std::function<void(const std::string&)> cb) = 0;
  virtual void RegisterRecvTrailingMetadata(
      StreamIdentifier id, std::function<void(const Metadata&, int)> cb) = 0;

  // TODO(mingcl): Provide a way to unregister callback?

  // TODO(mingcl): Figure out how to handle the case where there is no callback
  // registered for the stream. For now, I don't see this case happening in
  // unary calls. So we would probably just crash the program for now.
  // For streaming calls it does happen, for now we simply queue them.
  virtual void NotifyRecvInitialMetadata(StreamIdentifier id,
                                         const Metadata& initial_metadata) = 0;
  virtual void NotifyRecvMessage(StreamIdentifier id,
                                 const std::string& message) = 0;
  virtual void NotifyRecvTrailingMetadata(StreamIdentifier id,
                                          const Metadata& trailing_metadata,
                                          int status) = 0;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_UTILS_TRANSPORT_STREAM_RECEIVER_H
