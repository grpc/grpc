/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_METHOD_PARAMS_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_METHOD_PARAMS_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/exec_ctx.h"  // for grpc_millis
#include "src/core/lib/json/json.h"

namespace grpc_core {
namespace internal {

class ClientChannelMethodParams : public RefCounted<ClientChannelMethodParams> {
 public:
  enum WaitForReady {
    WAIT_FOR_READY_UNSET = 0,
    WAIT_FOR_READY_FALSE,
    WAIT_FOR_READY_TRUE
  };

  /// Creates a method_parameters object from \a json.
  /// Intended for use with ServiceConfig::CreateMethodConfigTable().
  static RefCountedPtr<ClientChannelMethodParams> CreateFromJson(
      const grpc_json* json);

  grpc_millis timeout() const { return timeout_; }
  WaitForReady wait_for_ready() const { return wait_for_ready_; }

 private:
  // So New() can call our private ctor.
  template <typename T, typename... Args>
  friend T* grpc_core::New(Args&&... args);

  ClientChannelMethodParams() {}
  virtual ~ClientChannelMethodParams() {}

  grpc_millis timeout_ = 0;
  WaitForReady wait_for_ready_ = WAIT_FOR_READY_UNSET;
};

}  // namespace internal
}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_METHOD_PARAMS_H */
