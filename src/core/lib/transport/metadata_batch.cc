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

#include <grpc/support/port_platform.h>

#include "src/core/lib/transport/metadata_batch.h"

#include <stdbool.h>
#include <string.h>

#include "absl/container/inlined_vector.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/slice/slice_string_helpers.h"

namespace {

class CopySink {
 public:
  explicit CopySink(grpc_metadata_batch* dst) : dst_(dst) {}

  template <class T, class V>
  void Encode(T trait, V value) {
    dst_->Set(trait, value);
  }

  template <class T>
  void Encode(T trait, const grpc_core::Slice& value) {
    dst_->Set(trait, std::move(value.AsOwned()));
  }

 private:
  grpc_metadata_batch* dst_;
};

}  // namespace
