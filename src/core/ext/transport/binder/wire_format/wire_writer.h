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

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_WRITER_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_WRITER_H

#include <grpc/impl/codegen/port_platform.h>

#include <string>
#include <vector>

#include <grpc/support/log.h>

#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/transaction.h"
#include "src/core/lib/gprpp/sync.h"

namespace grpc_binder {

class WireWriter {
 public:
  virtual ~WireWriter() = default;
  virtual absl::Status RpcCall(const Transaction& call) = 0;
  virtual absl::Status Ack(int64_t num_bytes) = 0;
};

class WireWriterImpl : public WireWriter {
 public:
  explicit WireWriterImpl(std::unique_ptr<Binder> binder);
  absl::Status RpcCall(const Transaction& tx) override;
  absl::Status Ack(int64_t num_bytes) override;

 private:
  grpc_core::Mutex mu_;
  std::unique_ptr<Binder> binder_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_WRITER_H
